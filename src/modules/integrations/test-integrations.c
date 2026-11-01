#include "e-util/e-util.h"
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <libsecret/secret.h>
#include <glib/gstdio.h>

typedef struct {
	const gchar *name, *site;
	gboolean wrong_project, wrong_external, read_only, missing_resolver;
} TrackingProbeCase;

static const TrackingProbeCase *tracking_probe_case;
static guint tracking_probe_registrations;
static gboolean tracking_history_test, tracking_history_fail;
static guint tracking_history_requests;

gchar *
secret_password_lookup_sync (const SecretSchema *schema, GCancellable *cancellable, GError **error, ...)
{
	return g_strdup ("synthetic-probe-key");
}

void
soup_message_set_request_body_from_bytes (SoupMessage *message, const gchar *content_type, GBytes *bytes)
{
	g_object_set_data_full (G_OBJECT (message), "test-body", g_bytes_ref (bytes), (GDestroyNotify) g_bytes_unref);
}

SoupStatus
soup_message_get_status (SoupMessage *message)
{
	return GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (message), "test-status"));
}

GBytes *
soup_session_send_and_read (SoupSession *session, SoupMessage *message, GCancellable *cancellable, GError **error)
{
	const gchar *path = g_uri_get_path (soup_message_get_uri (message));
	gchar *response;
	guint status = 200;
	g_assert_nonnull (tracking_probe_case);
	if (tracking_history_test)
		tracking_history_requests++;
	if (tracking_history_fail) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE, "Synthetic API failure");
		return NULL;
	}
	if (g_str_has_suffix (path, "/summary")) {
		response = g_strdup (tracking_history_test ?
			"{\"project_id\":\"Ab3dE7fG9h\",\"count\":2,\"first_request\":\"2026-11-01T10:00:00Z\",\"latest_request\":\"2026-11-01T11:00:00Z\"}" :
			"{\"project_id\":\"Ab3dE7fG9h\",\"count\":0}");
	} else if (g_str_has_suffix (path, "/events")) {
		GHashTable *params = g_uri_parse_params (g_uri_get_query (soup_message_get_uri (message)), -1, "&", G_URI_PARAMS_NONE, NULL);
		const gchar *external = g_hash_table_lookup (params, "external_id");
		g_assert_nonnull (g_hash_table_lookup (params, "to"));
		response = g_strdup_printf ("{\"project_id\":\"Ab3dE7fG9h\",\"events\":[{\"id\":2,\"external_id\":\"%s\",\"requested_at\":\"2026-11-01T11:00:00Z\"},{\"id\":1,\"external_id\":\"%s\",\"requested_at\":\"2026-11-01T10:00:00Z\"}]}", external, external);
		g_hash_table_unref (params);
	} else if (g_str_has_suffix (path, "/messages/resolve")) {
		status = 404;
		response = g_strdup (tracking_probe_case->missing_resolver ?
			"{\"code\":\"rest_no_route\"}" : "{\"code\":\"wtp_message_not_found\"}");
	} else {
		JsonParser *parser = json_parser_new ();
		GBytes *body = g_object_get_data (G_OBJECT (message), "test-body");
		gsize length;
		const gchar *text = g_bytes_get_data (body, &length);
		const gchar *external_id;
		gchar *token = g_strnfill (64, 'T');
		g_assert_true (g_str_has_suffix (path, "/messages"));
		g_assert_cmpstr (soup_message_get_method (message), ==, "POST");
		g_assert_true (json_parser_load_from_data (parser, text, length, NULL));
		external_id = json_object_get_string_member (json_node_get_object (json_parser_get_root (parser)), "external_id");
		g_assert_cmpuint (strlen (external_id), ==, 128);
		g_assert_true (g_str_has_prefix (external_id, "BlueEvolutionConnectionTest"));
		tracking_probe_registrations++;
		status = tracking_probe_case->read_only ? 401 : 200;
		response = g_strdup_printf ("{\"project_id\":\"%s\",\"external_id\":\"%s\",\"tracking_url\":\"%s/?wtp_pixel=Ab3dE7fG9h.%s\"}",
			tracking_probe_case->wrong_project ? "OtherPrj01" : "Ab3dE7fG9h",
			tracking_probe_case->wrong_external ? "wrong" : external_id, tracking_probe_case->site, token);
		g_free (token);
		g_object_unref (parser);
	}
	g_object_set_data (G_OBJECT (message), "test-status", GUINT_TO_POINTER (status));
	return g_bytes_new_take (response, strlen (response));
}

static guint saved_keys;
static gboolean tracking_key_saved;

gboolean
e_wordpress_tracking_manager_has_key (EWordPressTrackingManager *manager, GError **error)
{
	return tracking_key_saved;
}

gboolean
e_wordpress_tracking_manager_store_key (EWordPressTrackingManager *manager, const gchar *key, GError **error)
{
	g_assert_cmpstr (key, ==, "synthetic-tracking-key");
	tracking_key_saved = TRUE;
	return TRUE;
}

gboolean
e_wordpress_tracking_manager_remove_key (EWordPressTrackingManager *manager, GError **error)
{
	tracking_key_saved = FALSE;
	return TRUE;
}

void
e_openai_manager_request (EOpenAIManager *manager, EOpenAIRequest kind,
                         const gchar *email, const gchar *prompt, const gchar *history,
                         GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
	GTask *task = g_task_new (manager, cancellable, callback, data);
	g_assert_true (kind == E_OPENAI_HAS_KEY || kind == E_OPENAI_TEST_KEY);
	g_assert_null (email);
	g_task_return_pointer (task, g_strdup (kind == E_OPENAI_HAS_KEY ?
		(g_str_equal (e_openai_manager_get_provider (manager), "openai") ? "configured" : "") : "valid"), g_free);
	g_object_unref (task);
}

void
e_openai_manager_set_key (EOpenAIManager *manager, const gchar *key, gboolean consent,
                         GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
	GTask *task = g_task_new (manager, cancellable, callback, data);
	g_assert_true (consent);
	g_assert_cmpstr (key, ==, "synthetic-test-key");
	saved_keys++;
	g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}

#include "evolution-integrations.c"

static void
tracking_probe_done (GObject *source, GAsyncResult *result, gpointer data)
{
	GAsyncResult **completed = data;
	*completed = g_object_ref (result);
}

static void
test_tracking_connection_probe (gconstpointer data)
{
	EWordPressTrackingManager *manager = e_wordpress_tracking_manager_new ();
	GAsyncResult *result = NULL;
	GError *error = NULL;
	gboolean success;
	tracking_probe_case = data;
	tracking_probe_registrations = 0;
	e_wordpress_tracking_manager_set_site_url (manager, "https://tracking.example.test/wordpress");
	e_wordpress_tracking_manager_set_project_id (manager, "Ab3dE7fG9h");
	e_wordpress_tracking_manager_test_async (manager, NULL, tracking_probe_done, &result);
	while (!result)
		g_main_context_iteration (NULL, TRUE);
	success = e_wordpress_tracking_manager_test_finish (manager, result, &error);
	if (g_str_equal (tracking_probe_case->name, "matching-url")) {
		g_assert_true (success);
		g_assert_no_error (error);
	} else {
		g_assert_false (success);
		g_assert_nonnull (error);
	}
	g_assert_cmpuint (tracking_probe_registrations, ==, tracking_probe_case->missing_resolver ? 0 : 1);
	g_clear_error (&error);
	g_object_unref (result);
	g_object_unref (manager);
	tracking_probe_case = NULL;
}

static void
test_tracking_history_offline (void)
{
	static const TrackingProbeCase fixture = { "history", "https://tracking.example.test/wordpress", FALSE, FALSE, FALSE, FALSE };
	EWordPressTrackingManager *manager = e_wordpress_tracking_manager_new ();
	EWordPressTrackingSummary *summary;
	GAsyncResult *result = NULL;
	GError *error = NULL;
	gchar *external = g_strnfill (128, 'H');
	gchar *digest, *filename, *path, *contents = NULL;
	GStatBuf statbuf;
	tracking_probe_case = &fixture;
	tracking_history_test = TRUE;
	tracking_history_requests = 0;
	e_wordpress_tracking_manager_set_site_url (manager, fixture.site);
	e_wordpress_tracking_manager_set_project_id (manager, "Ab3dE7fG9h");
	e_wordpress_tracking_manager_summary_async (manager, external, NULL, tracking_probe_done, &result);
	while (!result)
		g_main_context_iteration (NULL, TRUE);
	summary = e_wordpress_tracking_manager_summary_finish (manager, result, &error);
	g_assert_no_error (error);
	g_assert_nonnull (summary);
	g_assert_cmpuint (e_wordpress_tracking_summary_get_count (summary), ==, 2);
	g_assert_cmpuint (e_wordpress_tracking_summary_get_request_times (summary)->len, ==, 2);
	g_assert_true (e_wordpress_tracking_summary_get_history_complete (summary));
	e_wordpress_tracking_summary_free (summary);
	g_clear_object (&result);
	g_object_unref (manager);
	manager = e_wordpress_tracking_manager_new ();
	summary = e_wordpress_tracking_manager_load_history (manager, external, &error);
	g_assert_no_error (error);
	g_assert_nonnull (summary);
	g_assert_cmpuint (e_wordpress_tracking_summary_get_count (summary), ==, 2);
	g_assert_cmpstr (g_ptr_array_index (e_wordpress_tracking_summary_get_request_times (summary), 0), ==, "2026-11-01T11:00:00Z");
	e_wordpress_tracking_summary_free (summary);
	digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, "https://tracking.example.test/wordpress\nAb3dE7fG9h", -1);
	filename = g_strconcat (external, ".json", NULL);
	path = g_build_filename (g_get_user_data_dir (), "blueevolution", "tracking-history", digest, filename, NULL);
	g_assert_cmpint (g_stat (path, &statbuf), ==, 0);
	g_assert_cmpuint (statbuf.st_mode & 0777, ==, 0600);
	g_assert_true (g_file_get_contents (path, &contents, NULL, &error));
	g_assert_no_error (error);
	g_assert_null (strstr (contents, "synthetic-probe-key"));
	g_assert_null (strstr (contents, "wtp_pixel"));
	tracking_history_fail = TRUE;
	e_wordpress_tracking_manager_summary_async (manager, external, NULL, tracking_probe_done, &result);
	while (!result)
		g_main_context_iteration (NULL, TRUE);
	summary = e_wordpress_tracking_manager_summary_finish (manager, result, &error);
	g_assert_null (summary);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE);
	g_clear_error (&error);
	g_assert_cmpuint (tracking_history_requests, ==, 3);
	summary = e_wordpress_tracking_manager_load_history (manager, external, &error);
	g_assert_no_error (error);
	g_assert_nonnull (summary);
	g_assert_cmpuint (e_wordpress_tracking_summary_get_count (summary), ==, 2);
	e_wordpress_tracking_summary_free (summary);
	e_wordpress_tracking_manager_set_site_url (manager, "https://other.example.test/");
	g_assert_null (e_wordpress_tracking_manager_load_history (manager, external, &error));
	g_assert_no_error (error);
	e_wordpress_tracking_manager_set_site_url (manager, fixture.site);
	g_assert_true (g_file_set_contents (path, "{}", -1, &error));
	g_assert_null (e_wordpress_tracking_manager_load_history (manager, external, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);
	g_remove (path);
	g_free (contents);
	g_free (path);
	g_free (filename);
	g_free (digest);
	g_free (external);
	g_object_unref (manager);
	g_object_unref (result);
	tracking_history_test = FALSE;
	tracking_history_fail = FALSE;
	tracking_probe_case = NULL;
}

static void
test_tracking_settings_controls (void)
{
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *widget = tracking_page_new (NULL);
	TrackingPage *page = tracking_page_from_widget (widget);
	GtkWidget *hint = gtk_grid_get_child_at (GTK_GRID (gtk_widget_get_parent (page->site_url)), 1, 1);
	g_assert_true (GTK_IS_LABEL (hint));
	g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (hint)), ==,
		_("Use the exact HTTPS Site Address (URL) from WordPress Settings > General, including www, port, and any subdirectory. The connection test also checks the generated pixel URL."));
	g_assert_true (gtk_label_get_line_wrap (GTK_LABEL (hint)));
	gtk_container_add (GTK_CONTAINER (window), widget);
	gtk_window_set_default_size (GTK_WINDOW (window), 1100, 650);
	gtk_widget_show_all (window);
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_cmpint (gtk_widget_get_allocated_width (page->site_url), >=, 500);
	g_assert_cmpint (gtk_widget_get_allocated_width (page->api_key), >=, 500);
	g_assert_cmpint (gtk_widget_get_allocated_width (page->project_id), ==,
		gtk_widget_get_allocated_width (page->site_url));
	g_assert_false (gtk_widget_get_sensitive (page->test));
	g_assert_false (gtk_widget_get_sensitive (page->remove));
	g_assert_true (gtk_widget_get_hexpand (page->site_url));
	g_assert_true (gtk_widget_get_hexpand (page->project_id));
	g_assert_true (gtk_widget_get_hexpand (page->api_key));
	g_assert_cmpint (gtk_entry_get_width_chars (GTK_ENTRY (page->site_url)), >=, 48);
	g_assert_cmpint (gtk_entry_get_width_chars (GTK_ENTRY (page->api_key)), >=, 48);
	gtk_entry_set_text (GTK_ENTRY (page->site_url), "https://tracking.example.test/wordpress");
	gtk_entry_set_text (GTK_ENTRY (page->project_id), "Ab3dE7fG9h");
	gtk_entry_set_text (GTK_ENTRY (page->api_key), "synthetic-tracking-key");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->enabled), TRUE);
	g_assert_true (gtk_widget_get_sensitive (page->default_enabled));
	g_assert_false (gtk_widget_get_sensitive (page->test));
	gtk_button_clicked (GTK_BUTTON (page->save));
	g_assert_true (gtk_widget_get_sensitive (page->test));
	g_assert_true (gtk_widget_get_sensitive (page->remove));
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (page->api_key)), ==, "");
	g_assert_cmpstr (gtk_entry_get_placeholder_text (GTK_ENTRY (page->api_key)), ==,
		_("API key saved (hidden). Enter a new key to replace it."));
	gtk_entry_set_text (GTK_ENTRY (page->site_url), "https://unsaved.example.test/");
	g_assert_false (gtk_widget_get_sensitive (page->test));
	gtk_entry_set_text (GTK_ENTRY (page->site_url), "https://tracking.example.test/wordpress");
	g_assert_true (gtk_widget_get_sensitive (page->test));
	tracking_page_busy (page, TRUE);
	g_assert_false (gtk_widget_get_sensitive (page->test));
	tracking_page_busy (page, FALSE);
	g_assert_true (gtk_widget_get_sensitive (page->test));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->clear_key), TRUE);
	g_assert_false (gtk_widget_get_sensitive (page->test));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->clear_key), FALSE);
	g_assert_true (gtk_widget_get_sensitive (page->test));
	gtk_button_clicked (GTK_BUTTON (page->remove));
	g_assert_false (gtk_widget_get_sensitive (page->test));
	g_assert_false (gtk_widget_get_sensitive (page->remove));
	g_assert_false (gtk_widget_get_sensitive (page->default_enabled));
	gtk_widget_destroy (window);
}

static void
test_tracking_ids_and_pixel_cleanup (void)
{
	EWordPressTrackingManager *manager = e_wordpress_tracking_manager_new ();
	CamelMimeMessage *message = camel_mime_message_new ();
	CamelMultipart *alternative = camel_multipart_new ();
	CamelMultipart *mixed = camel_multipart_new ();
	CamelMimePart *body_part = camel_mime_part_new ();
	CamelMimePart *attachment = camel_mime_part_new ();
	CamelMimePart *plain = camel_mime_part_new ();
	CamelMimePart *html = camel_mime_part_new ();
	CamelStream *stream;
	GByteArray *decoded;
	GError *error = NULL;
	const gchar *body;
	gchar *external_id, *result, *token, *html_body, *attachment_body;
	gchar *parsed_project = NULL, *parsed_external = NULL, *header;

	external_id = e_wordpress_tracking_manager_generate_external_id ();
	g_assert_cmpuint (strlen (external_id), ==, 128);
	for (body = external_id; *body; body++)
		g_assert_true (g_ascii_isalnum (*body));
	header = g_strdup_printf ("v=1; project=Ab3dE7fG9h; external=%s", external_id);
	g_assert_true (tracking_parse_header (header, &parsed_project, &parsed_external));
	g_assert_cmpstr (parsed_project, ==, "Ab3dE7fG9h");
	g_assert_cmpstr (parsed_external, ==, external_id);
	g_free (parsed_project);
	g_free (parsed_external);
	parsed_project = NULL;
	parsed_external = NULL;
	g_free (header);
	{
		gchar *other_external = g_strdup (external_id);
		other_external[0] = other_external[0] == 'A' ? 'B' : 'A';
		header = g_strdup_printf ("v=1; project=Ab3dE7fG9h; external=%s; external=%s",
			external_id, other_external);
		g_assert_false (tracking_parse_header (header, &parsed_project, &parsed_external));
		g_assert_null (parsed_project);
		g_assert_null (parsed_external);
		g_free (other_external);
		g_free (header);
	}
	g_free (external_id);

	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (alternative), "multipart/alternative");
	camel_mime_part_set_content (plain, "Hello", -1, "text/plain; charset=utf-8");
	token = g_strnfill (64, 'T');
	html_body = g_strdup_printf ("<p>Hello</p><img src=\"cid:logo\"><img src=\"https://old.example/?wtp_pixel=Ab3dE7fG9h.%s\">", token);
	attachment_body = g_strdup_printf ("<p>Attached</p><img src=\"https://old.example/?wtp_pixel=Ab3dE7fG9h.%s\">", token);
	g_free (token);
	camel_mime_part_set_content (html, html_body, -1, "text/html; charset=utf-8");
	g_free (html_body);
	camel_mime_part_set_content (attachment, attachment_body, -1, "text/html; charset=utf-8");
	g_free (attachment_body);
	camel_mime_part_set_disposition (attachment, "attachment");
	camel_multipart_add_part (alternative, plain);
	camel_multipart_add_part (alternative, html);
	camel_medium_set_content (CAMEL_MEDIUM (body_part), CAMEL_DATA_WRAPPER (alternative));
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (mixed), "multipart/mixed");
	camel_multipart_add_part (mixed, body_part);
	camel_multipart_add_part (mixed, attachment);
	camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (mixed));

	g_assert_true (e_wordpress_tracking_manager_prepare_message (manager, message, FALSE, &error));
	g_assert_no_error (error);
	stream = camel_stream_mem_new ();
	g_assert_cmpint (camel_data_wrapper_decode_to_stream_sync (
		camel_medium_get_content (CAMEL_MEDIUM (html)), stream, NULL, &error), >=, 0);
	g_assert_no_error (error);
	decoded = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));
	result = g_strndup ((const gchar *) decoded->data, decoded->len);
	g_assert_null (strstr (result, "wtp_pixel="));
	g_assert_nonnull (strstr (result, "src=\"cid:logo\""));
	g_assert_nonnull (strstr (result, "<p>Hello</p>"));
	g_free (result);
	g_object_unref (stream);
	stream = camel_stream_mem_new ();
	g_assert_cmpint (camel_data_wrapper_decode_to_stream_sync (
		camel_medium_get_content (CAMEL_MEDIUM (attachment)), stream, NULL, &error), >=, 0);
	g_assert_no_error (error);
	decoded = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));
	result = g_strndup ((const gchar *) decoded->data, decoded->len);
	g_assert_nonnull (strstr (result, "wtp_pixel="));
	g_assert_nonnull (strstr (result, "<p>Attached</p>"));
	g_free (result);
	g_object_unref (stream);
	g_object_unref (plain);
	g_object_unref (html);
	g_object_unref (body_part);
	g_object_unref (attachment);
	g_object_unref (alternative);
	g_object_unref (mixed);
	g_object_unref (message);
	g_object_unref (manager);
}

static void
test_tracking_rejects_signed_messages (void)
{
	EWordPressTrackingManager *manager = e_wordpress_tracking_manager_new ();
	CamelMimeMessage *message = camel_mime_message_new ();
	CamelMultipart *signed_content = camel_multipart_new ();
	GError *error = NULL;

	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (signed_content), "multipart/signed");
	camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (signed_content));
	g_assert_false (e_wordpress_tracking_manager_prepare_message (manager, message, TRUE, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);
	g_object_unref (signed_content);
	g_object_unref (message);
	g_object_unref (manager);
}

static gboolean
respond_to_consent (gpointer response)
{
	GList *windows = gtk_window_list_toplevels (), *link;
	gboolean found = FALSE;
	for (link = windows; link; link = g_list_next (link)) {
		if (GTK_IS_MESSAGE_DIALOG (link->data)) {
			gchar *text = NULL, *title = NULL;
			g_object_get (link->data, "secondary-text", &text, "text", &title, NULL);
			gchar *expected = g_strdup_printf (_("Enable %s email assistance?"), "OpenAI");
			g_assert_cmpstr (title, ==, expected);
			g_free (expected);
			g_assert_nonnull (strstr (text, "OpenAI"));
			g_free (text);
			g_free (title);
			gtk_dialog_response (GTK_DIALOG (link->data), GPOINTER_TO_INT (response));
			found = TRUE;
			break;
		}
	}
	g_list_free (windows);
	g_assert_true (found);
	return G_SOURCE_REMOVE;
}

static void
test_key_controls (void)
{
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *widget = integrations_page_new (NULL);
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	const gchar *icon;
	GtkIconSize size;
	gtk_container_add (GTK_CONTAINER (window), widget);
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_true (gtk_label_get_selectable (GTK_LABEL (page->status)));
	g_assert_true (gtk_widget_get_sensitive (page->test));
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (page->entry)), ==, "");
	g_assert_cmpstr (gtk_entry_get_placeholder_text (GTK_ENTRY (page->entry)), ==,
		_("API key saved (hidden). Enter a new key to replace it."));
	g_assert_cmpstr (gtk_button_get_label (GTK_BUTTON (page->save)), ==, _("_Replace API Key"));
	gtk_button_clicked (GTK_BUTTON (page->test));
	while (g_main_context_iteration (NULL, FALSE));
	gtk_image_get_icon_name (GTK_IMAGE (page->feedback), &icon, &size);
	g_assert_cmpstr (icon, ==, "emblem-ok-symbolic");
	gtk_entry_set_text (GTK_ENTRY (page->entry), "synthetic-test-key");
	g_assert_false (gtk_entry_get_visibility (GTK_ENTRY (page->entry)));
	g_assert_false (gtk_widget_get_sensitive (page->test));
	g_assert_cmpint (gtk_image_get_storage_type (GTK_IMAGE (page->feedback)), ==, GTK_IMAGE_EMPTY);
	g_idle_add (respond_to_consent, GINT_TO_POINTER (GTK_RESPONSE_CANCEL));
	gtk_button_clicked (GTK_BUTTON (page->save));
	g_assert_cmpuint (saved_keys, ==, 0);
	g_idle_add (respond_to_consent, GINT_TO_POINTER (GTK_RESPONSE_ACCEPT));
	gtk_button_clicked (GTK_BUTTON (page->save));
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_cmpuint (saved_keys, ==, 1);
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (page->entry)), ==, "");
	g_assert_cmpstr (gtk_entry_get_placeholder_text (GTK_ENTRY (page->entry)), ==,
		_("API key saved (hidden). Enter a new key to replace it."));
	gtk_entry_set_text (GTK_ENTRY (page->entry), "unsaved-key");
	gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->provider), "gemini");
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_cmpstr (e_openai_manager_get_provider (page->manager), ==, "gemini");
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (page->entry)), ==, "");
	g_assert_false (page->configured);
	g_assert_false (gtk_widget_get_sensitive (page->test));
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (page->model)), ==, "gemini-2.5-flash");
	g_assert_cmpstr (gtk_link_button_get_uri (GTK_LINK_BUTTON (page->account)), ==, "https://aistudio.google.com/apikey");
	gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->provider), "claude");
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (page->model)), ==, "claude-haiku-4-5");
	gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->provider), "openai");
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_true (page->configured);
	gtk_widget_destroy (window);
}

int main (int argc, char **argv)
{
	static const TrackingProbeCase probe_cases[] = {
		{ "matching-url", "https://tracking.example.test/wordpress", FALSE, FALSE, FALSE, FALSE },
		{ "www-mismatch", "https://www.tracking.example.test/wordpress", FALSE, FALSE, FALSE, FALSE },
		{ "port-mismatch", "https://tracking.example.test:444/wordpress", FALSE, FALSE, FALSE, FALSE },
		{ "path-mismatch", "https://tracking.example.test/other", FALSE, FALSE, FALSE, FALSE },
		{ "http-url", "http://tracking.example.test/wordpress", FALSE, FALSE, FALSE, FALSE },
		{ "project-mismatch", "https://tracking.example.test/wordpress", TRUE, FALSE, FALSE, FALSE },
		{ "external-mismatch", "https://tracking.example.test/wordpress", FALSE, TRUE, FALSE, FALSE },
		{ "read-only-key", "https://tracking.example.test/wordpress", FALSE, FALSE, TRUE, FALSE },
		{ "missing-resolver", "https://tracking.example.test/wordpress", FALSE, FALSE, FALSE, TRUE }
	};
	guint index;
	gchar *test_data_dir = g_dir_make_tmp ("blueevolution-tracking-tests-XXXXXX", NULL);
	g_assert_nonnull (test_data_dir);
	g_setenv ("XDG_DATA_HOME", test_data_dir, TRUE);
	g_free (test_data_dir);
	gtk_init (&argc, &argv);
	if (g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR")) {
		bindtextdomain (GETTEXT_PACKAGE, g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR"));
		bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
		if (g_strcmp0 (g_getenv ("LANGUAGE"), "de") == 0)
			g_assert_cmpstr (_("Integrations"), ==, "Integrationen");
	}
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/openai/settings/consent-and-feedback", test_key_controls);
	g_test_add_func ("/wordpress-tracking/settings/save-and-controls", test_tracking_settings_controls);
	g_test_add_func ("/wordpress-tracking/history/offline-snapshot", test_tracking_history_offline);
	for (index = 0; index < G_N_ELEMENTS (probe_cases); index++) {
		gchar *path = g_strconcat ("/wordpress-tracking/connection/", probe_cases[index].name, NULL);
		g_test_add_data_func (path, &probe_cases[index], test_tracking_connection_probe);
		g_free (path);
	}
	g_test_add_func ("/wordpress-tracking/mime/strip-inherited-pixel", test_tracking_ids_and_pixel_cleanup);
	g_test_add_func ("/wordpress-tracking/mime/reject-signed-message", test_tracking_rejects_signed_messages);
	return g_test_run ();
}