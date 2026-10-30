#include "evolution-config.h"

#include <locale.h>
#include <time.h>
#include <glib/gi18n.h>
#include <libedataserver/libedataserver.h>

#include "e-mail-config-provider-page.h"

typedef struct {
	CamelOfflineStore parent;
} PolicyStore;

typedef CamelOfflineStoreClass PolicyStoreClass;

typedef struct {
	CamelOfflineFolder parent;
	GHashTable *messages;
	GHashTable *cached;
	gint downloads;
	gint downsyncs;
} PolicyFolder;

typedef CamelOfflineFolderClass PolicyFolderClass;

typedef struct {
	CamelSession parent;
} PolicySession;

typedef CamelSessionClass PolicySessionClass;

GType policy_store_get_type (void);
GType policy_folder_get_type (void);
GType policy_session_get_type (void);

G_DEFINE_TYPE (PolicyStore, policy_store, CAMEL_TYPE_OFFLINE_STORE)
G_DEFINE_TYPE (PolicyFolder, policy_folder, CAMEL_TYPE_OFFLINE_FOLDER)
G_DEFINE_TYPE (PolicySession, policy_session, CAMEL_TYPE_SESSION)

static void
policy_session_class_init (PolicySessionClass *class)
{
}

static void
policy_session_init (PolicySession *session)
{
}

static void
policy_wait_for_counter (gint *counter,
			gint expected)
{
	gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;

	while (g_atomic_int_get (counter) < expected && g_get_monotonic_time () < deadline) {
		while (g_main_context_iteration (NULL, FALSE));
		g_usleep (1000);
	}
	g_assert_cmpint (g_atomic_int_get (counter), ==, expected);
	while (g_main_context_iteration (NULL, FALSE));
}

static void
policy_job_finished_cb (CamelSession *session,
			GCancellable *cancellable,
			const GError *error,
			gpointer user_data)
{
	g_assert_no_error (error);
	g_atomic_int_inc ((gint *) user_data);
}

static gboolean
policy_store_connect_sync (CamelService *service,
			   GCancellable *cancellable,
			   GError **error)
{
	return TRUE;
}

static void
policy_store_class_init (PolicyStoreClass *class)
{
	CAMEL_SERVICE_CLASS (class)->settings_type = CAMEL_TYPE_OFFLINE_SETTINGS;
	CAMEL_SERVICE_CLASS (class)->connect_sync = policy_store_connect_sync;
}

static void
policy_store_init (PolicyStore *store)
{
}

static GPtrArray *
policy_folder_dup_uids (CamelFolder *folder)
{
	PolicyFolder *self = (PolicyFolder *) folder;
	GHashTableIter iter;
	gpointer key;
	GPtrArray *uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);

	g_hash_table_iter_init (&iter, self->messages);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (uids, (gpointer) camel_pstring_strdup (key));
	return uids;
}

static CamelMessageInfo *
policy_folder_get_message_info (CamelFolder *folder,
				const gchar *uid)
{
	CamelMessageInfo *info = g_hash_table_lookup (((PolicyFolder *) folder)->messages, uid);

	return info ? g_object_ref (info) : NULL;
}

static GPtrArray *
policy_folder_dup_uncached_uids (CamelFolder *folder,
				GPtrArray *uids,
				GError **error)
{
	PolicyFolder *self = (PolicyFolder *) folder;
	GPtrArray *uncached = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
	guint index;

	for (index = 0; index < uids->len; index++) {
		if (!g_hash_table_contains (self->cached, uids->pdata[index]))
			g_ptr_array_add (uncached, (gpointer) camel_pstring_strdup (uids->pdata[index]));
	}
	return uncached;
}

static gboolean
policy_folder_synchronize_message_sync (CamelFolder *folder,
					const gchar *uid,
					GCancellable *cancellable,
					GError **error)
{
	g_hash_table_add (((PolicyFolder *) folder)->cached, g_strdup (uid));
	g_atomic_int_inc (&((PolicyFolder *) folder)->downloads);
	return TRUE;
}

static gboolean
policy_folder_refresh_info_sync (CamelFolder *folder,
				 GCancellable *cancellable,
				 GError **error)
{
	return TRUE;
}

static gboolean
policy_folder_downsync_sync (CamelOfflineFolder *folder,
			    const gchar *expression,
			    GCancellable *cancellable,
			    GError **error)
{
	gboolean success;

	if (expression)
		g_assert_cmpstr (expression, ==, "(match-all (not (system-flag \"Seen\")))");
	success = CAMEL_OFFLINE_FOLDER_CLASS (policy_folder_parent_class)->downsync_sync (folder, NULL, cancellable, error);
	g_atomic_int_inc (&((PolicyFolder *) folder)->downsyncs);
	return success;
}

static CamelMimeMessage *
policy_folder_get_message_sync (CamelFolder *folder,
				const gchar *uid,
				GCancellable *cancellable,
				GError **error)
{
	CamelMimeMessage *message = camel_mime_message_new ();

	camel_mime_part_set_content (CAMEL_MIME_PART (message), "Synthetic message body", -1, "text/plain");
	return message;
}

static void
policy_folder_finalize (GObject *object)
{
	PolicyFolder *self = (PolicyFolder *) object;

	g_hash_table_unref (self->messages);
	g_hash_table_unref (self->cached);
	G_OBJECT_CLASS (policy_folder_parent_class)->finalize (object);
}

static void
policy_folder_class_init (PolicyFolderClass *class)
{
	CamelFolderClass *folder_class = CAMEL_FOLDER_CLASS (class);

	G_OBJECT_CLASS (class)->finalize = policy_folder_finalize;
	folder_class->dup_uids = policy_folder_dup_uids;
	folder_class->get_message_info = policy_folder_get_message_info;
	folder_class->dup_uncached_uids = policy_folder_dup_uncached_uids;
	folder_class->synchronize_message_sync = policy_folder_synchronize_message_sync;
	folder_class->get_message_sync = policy_folder_get_message_sync;
	folder_class->refresh_info_sync = policy_folder_refresh_info_sync;
	CAMEL_OFFLINE_FOLDER_CLASS (class)->downsync_sync = policy_folder_downsync_sync;
}

static void
policy_folder_init (PolicyFolder *self)
{
	self->messages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	self->cached = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
test_policy_downloads (void)
{
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	PolicyFolder *folder;
	CamelMessageInfo *info;
	GError *error = NULL;
	gchar *directory = g_dir_make_tmp ("blueevolution-policy-XXXXXX", &error);
	const gchar *uids[] = { "unread", "read", "old", "deleted", "junk" };
	guint index;
	static CamelProvider provider = {
		.protocol = "blueevolution-policy-test",
		.name = "Synthetic download policy",
		.flags = CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE | CAMEL_PROVIDER_IS_STORAGE
	};

	g_assert_no_error (error);
	provider.object_types[CAMEL_PROVIDER_STORE] = policy_store_get_type ();
	camel_provider_register (&provider);
	session = g_object_new (policy_session_get_type (), "user-data-dir", directory, "user-cache-dir", directory, NULL);
	service = camel_session_add_service (session, "synthetic-policy", provider.protocol, CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (service);
	settings = camel_service_ref_settings (service);
	g_assert_nonnull (g_object_class_find_property (G_OBJECT_GET_CLASS (settings), "download-unread-only"));
	folder = g_object_new (policy_folder_get_type (),
		"parent-store", service, "full-name", "INBOX", "display-name", "Synthetic Inbox", NULL);
	for (index = 0; index < G_N_ELEMENTS (uids); index++) {
		info = camel_message_info_new (NULL);
		camel_message_info_set_uid (info, uids[index]);
		camel_message_info_set_date_sent (info, index == 2 ? 1 : time (NULL));
		camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_JUNK,
			index == 1 ? CAMEL_MESSAGE_SEEN : (index == 3 ? CAMEL_MESSAGE_DELETED : (index == 4 ? CAMEL_MESSAGE_JUNK : 0)));
		g_hash_table_insert (folder->messages, g_strdup (uids[index]), info);
	}

	g_assert_false (camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder)));
	g_object_set (settings, "download-unread-only", TRUE, "limit-by-age", TRUE,
		"limit-unit", CAMEL_TIME_UNIT_DAYS, "limit-value", 30, NULL);
	g_assert_true (camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder)));
	g_assert_true (camel_offline_folder_downsync_sync (CAMEL_OFFLINE_FOLDER (folder), NULL, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (g_hash_table_size (folder->cached), ==, 1);
	g_assert_true (g_hash_table_contains (folder->cached, "unread"));
	info = g_hash_table_lookup (folder->messages, "unread");
	g_assert_false (camel_message_info_get_flags (info) & CAMEL_MESSAGE_SEEN);

	info = g_hash_table_lookup (folder->messages, "read");
	camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, 0);
	g_assert_true (camel_offline_folder_downsync_sync (CAMEL_OFFLINE_FOLDER (folder), NULL, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (g_hash_table_size (folder->cached), ==, 2);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_assert_true (g_hash_table_contains (folder->cached, "read"));

	camel_offline_folder_set_offline_sync (CAMEL_OFFLINE_FOLDER (folder), CAMEL_THREE_STATE_OFF);
	g_assert_false (camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder)));
	camel_offline_folder_set_offline_sync (CAMEL_OFFLINE_FOLDER (folder), CAMEL_THREE_STATE_ON);
	g_assert_true (camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder)));
	g_assert_true (camel_offline_folder_downsync_sync (CAMEL_OFFLINE_FOLDER (folder), NULL, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (g_hash_table_size (folder->cached), ==, 4);

	g_hash_table_remove_all (folder->cached);
	camel_offline_folder_set_offline_sync (CAMEL_OFFLINE_FOLDER (folder), CAMEL_THREE_STATE_INCONSISTENT);
	g_object_set (settings, "stay-synchronized", TRUE, "download-unread-only", FALSE, "limit-by-age", FALSE, NULL);
	g_assert_true (camel_offline_folder_downsync_sync (CAMEL_OFFLINE_FOLDER (folder), NULL, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (g_hash_table_size (folder->cached), ==, 5);

	g_hash_table_remove_all (folder->cached);
	g_object_set (settings, "stay-synchronized", FALSE, "download-unread-only", TRUE, NULL);
	{
		gint downsyncs = g_atomic_int_get (&folder->downsyncs);
		gint downloads;
		gint finished_jobs = 0;
		gulong job_handler;
		CamelFolderChangeInfo *changes;

		g_assert_true (camel_folder_refresh_info_sync (CAMEL_FOLDER (folder), NULL, &error));
		g_assert_no_error (error);
		policy_wait_for_counter (&folder->downsyncs, downsyncs + 1);
		g_assert_cmpuint (g_hash_table_size (folder->cached), ==, 2);
		g_assert_true (g_hash_table_contains (folder->cached, "unread"));
		g_assert_true (g_hash_table_contains (folder->cached, "old"));

		downloads = g_atomic_int_get (&folder->downloads);
		job_handler = g_signal_connect (session, "job-finished", G_CALLBACK (policy_job_finished_cb), &finished_jobs);
		info = g_hash_table_lookup (folder->messages, "read");
		camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, 0);
		changes = camel_folder_change_info_new ();
		camel_folder_change_info_change_uid (changes, "read");
		g_signal_emit_by_name (folder, "changed", changes);
		policy_wait_for_counter (&finished_jobs, 1);
		g_assert_cmpint (g_atomic_int_get (&folder->downloads), ==, downloads + 1);
		g_assert_true (g_hash_table_contains (folder->cached, "read"));
		g_signal_emit_by_name (folder, "changed", changes);
		policy_wait_for_counter (&finished_jobs, 2);
		g_assert_cmpint (g_atomic_int_get (&folder->downloads), ==, downloads + 1);
		camel_folder_change_info_free (changes);

		downloads = g_atomic_int_get (&folder->downloads);
		info = camel_message_info_new (NULL);
		camel_message_info_set_uid (info, "new-unread");
		g_hash_table_insert (folder->messages, g_strdup ("new-unread"), info);
		changes = camel_folder_change_info_new ();
		camel_folder_change_info_add_uid (changes, "new-unread");
		camel_session_set_online (session, FALSE);
		g_signal_emit_by_name (folder, "changed", changes);
		g_assert_cmpint (g_atomic_int_get (&folder->downloads), ==, downloads);
		camel_session_set_online (session, TRUE);
		g_signal_emit_by_name (folder, "changed", changes);
		policy_wait_for_counter (&finished_jobs, 3);
		g_assert_cmpint (g_atomic_int_get (&folder->downloads), ==, downloads + 1);
		g_assert_true (g_hash_table_contains (folder->cached, "new-unread"));
		camel_folder_change_info_free (changes);
		g_signal_handler_disconnect (session, job_handler);
	}

	g_object_unref (folder);
	g_object_unref (settings);
	camel_session_remove_services (session);
	g_object_unref (service);
	g_object_unref (session);
	{
		GFile *file = g_file_new_for_path (directory);

		g_assert_true (e_file_recursive_delete_sync (file, NULL, &error));
		g_assert_no_error (error);
		g_object_unref (file);
	}
	g_free (directory);
}

static void
test_policy_settings (void)
{
	ESource *source;
	ESourceCamel *extension;
	CamelSettings *settings;
	GKeyFile *key_file = g_key_file_new ();
	GError *error = NULL;
	gchar *data;
	const gchar *extension_name;
	gboolean unread_only = TRUE;

	e_source_camel_generate_subtype ("blueevolution-policy-settings", CAMEL_TYPE_OFFLINE_SETTINGS);
	source = e_source_new (NULL, NULL, &error);
	g_assert_no_error (error);
	extension_name = e_source_camel_get_extension_name ("blueevolution-policy-settings");
	extension = e_source_get_extension (source, extension_name);
	settings = e_source_camel_get_settings (extension);
	g_object_get (settings, "download-unread-only", &unread_only, NULL);
	g_assert_false (unread_only);
	g_object_set (settings, "download-unread-only", TRUE, NULL);
	data = e_source_to_string (source, NULL);
	g_assert_true (g_key_file_load_from_data (key_file, data, -1, G_KEY_FILE_NONE, &error));
	g_assert_no_error (error);
	g_assert_true (g_key_file_get_boolean (key_file, extension_name, "DownloadUnreadOnly", &error));
	g_assert_no_error (error);
	g_free (data);
	g_key_file_unref (key_file);
	g_object_unref (source);
}

static void
test_policy_widgets (void)
{
	CamelSettings *settings = g_object_new (CAMEL_TYPE_OFFLINE_SETTINGS, NULL);
	CamelSettings *clone;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	GList *rows, *children;
	GtkComboBox *combo;
	gboolean all_messages, unread_only;
	CamelProviderConfEntry entries[] = {
		{ CAMEL_PROVIDER_CONF_CHECKBOX, "stay-synchronized", NULL, "Legacy synchronization" },
		{ CAMEL_PROVIDER_CONF_END }
	};
	CamelProvider provider = { .extra_conf = entries };

	g_object_ref_sink (box);
	e_mail_config_provider_add_widgets (&provider, settings, GTK_BOX (box), FALSE);
	rows = gtk_container_get_children (GTK_CONTAINER (box));
	g_assert_cmpuint (g_list_length (rows), ==, 1);
	children = gtk_container_get_children (GTK_CONTAINER (rows->data));
	g_assert_cmpstr (gtk_label_get_label (GTK_LABEL (children->data)), ==, _("Automatically _download messages:"));
	combo = GTK_COMBO_BOX (children->next->data);
	{
		GtkTreeModel *model = gtk_combo_box_get_model (combo);
		GtkTreeIter iter;
		const gchar *labels[] = { _("On demand"), _("Unread messages only"), _("All messages") };
		guint index;

		g_assert_cmpint (gtk_tree_model_iter_n_children (model, NULL), ==, 3);
		g_assert_true (gtk_tree_model_get_iter_first (model, &iter));
		for (index = 0; index < G_N_ELEMENTS (labels); index++) {
			gchar *text;

			gtk_tree_model_get (model, &iter, 0, &text, -1);
			g_assert_cmpstr (text, ==, labels[index]);
			g_free (text);
			gtk_tree_model_iter_next (model, &iter);
		}
		if (g_strcmp0 (g_getenv ("LANGUAGE"), "de") == 0)
			g_assert_cmpstr (labels[1], ==, "Nur ungelesene Nachrichten");
	}
	g_assert_cmpint (gtk_combo_box_get_active (combo), ==, 0);
	gtk_combo_box_set_active (combo, 1);
	g_object_get (settings, "stay-synchronized", &all_messages, "download-unread-only", &unread_only, NULL);
	g_assert_false (all_messages);
	g_assert_true (unread_only);
	clone = camel_settings_clone (settings);
	g_object_get (clone, "download-unread-only", &unread_only, NULL);
	g_assert_true (unread_only);
	g_object_unref (clone);
	gtk_combo_box_set_active (combo, 2);
	g_object_get (settings, "stay-synchronized", &all_messages, "download-unread-only", &unread_only, NULL);
	g_assert_true (all_messages);
	g_assert_false (unread_only);
	g_object_set (settings, "stay-synchronized", FALSE, NULL);
	g_assert_cmpint (gtk_combo_box_get_active (combo), ==, 0);
	g_list_free (children);
	g_list_free (rows);
	gtk_widget_destroy (box);
	g_object_unref (box);
	g_object_unref (settings);
}

static void
test_policy_unavailable (void)
{
	g_test_skip ("Requires the bundled Camel download-policy extension");
}

int
main (int argc,
	char **argv)
{
	GObjectClass *settings_class;
	gboolean available;

	setlocale (LC_ALL, "");
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	if (g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR"))
		bindtextdomain (GETTEXT_PACKAGE, g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR"));
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
	camel_provider_init ();
	settings_class = g_type_class_ref (CAMEL_TYPE_OFFLINE_SETTINGS);
	available = g_object_class_find_property (settings_class, "download-unread-only") != NULL;
	g_type_class_unref (settings_class);
	if (!available) {
		g_assert_null (g_getenv ("BLUEEVOLUTION_REQUIRE_DOWNLOAD_POLICY"));
		g_test_add_func ("/blueevolution/mail/download-policy/available", test_policy_unavailable);
		return g_test_run ();
	}
	g_test_add_func ("/blueevolution/mail/download-policy/downloads", test_policy_downloads);
	g_test_add_func ("/blueevolution/mail/download-policy/settings", test_policy_settings);
	g_test_add_func ("/blueevolution/mail/download-policy/widgets", test_policy_widgets);
	return g_test_run ();
}