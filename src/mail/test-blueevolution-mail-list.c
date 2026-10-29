#include "evolution-config.h"

#include <time.h>

#include "e-mail-list-cell.c"
#include "e-mail-request.c"
#define PROP_0 MESSAGE_LIST_PROP_0
#define N_PROPS MESSAGE_LIST_N_PROPS
#define properties message_list_properties
#include "message-list.c"
#undef properties
#undef N_PROPS
#undef PROP_0
#define PROP_0 PANED_VIEW_PROP_0
#define N_PROPS PANED_VIEW_N_PROPS
#define PROP_GROUP_BY_THREADS PANED_VIEW_PROP_GROUP_BY_THREADS
#define properties paned_view_properties
#include "e-mail-paned-view.c"
#undef properties
#undef PROP_GROUP_BY_THREADS
#undef N_PROPS
#undef PROP_0
#undef _
#undef Q_
#undef C_
#include "em-format/e-mail-formatter-headers.c"

static void
scroll_test_wait (guint milliseconds)
{
	gint64 end = g_get_monotonic_time () + milliseconds * 1000;

	while (g_get_monotonic_time () < end) {
		while (g_main_context_iteration (NULL, FALSE));
		g_usleep (1000);
	}
}

static void
test_mouse_wheel_scroll_frames (void)
{
	const guint rates[] = { 60, 120 };
	guint rate, frame;

	for (rate = 0; rate < G_N_ELEMENTS (rates); rate++) {
		MailListScroll scroll = { 0 };
		gboolean finished = FALSE;
		gdouble previous = 0.0;

		scroll.adjustment = gtk_adjustment_new (0, 0, 4000, 20, 200, 400);
		g_object_ref_sink (scroll.adjustment);
		scroll.target = 60.0;
		for (frame = 0; frame < rates[rate] && !finished; frame++) {
			gdouble value;

			finished = mail_list_scroll_advance (&scroll, previous, 1.0 / rates[rate]);
			value = gtk_adjustment_get_value (scroll.adjustment);
			g_assert_cmpfloat (value, >, previous);
			g_assert_cmpfloat (value, <=, scroll.target);
			/* No single frame may consume a large part of a wheel notch. */
			g_assert_cmpfloat (value - previous, <, 60.0 * 0.16);
			if (frame == 0)
				g_assert_cmpfloat (value, <, 60.0 * 0.07);
			previous = value;
		}
		g_assert_true (finished);
		g_assert_cmpfloat (previous, ==, scroll.target);
		g_object_unref (scroll.adjustment);
	}
}

static void
test_mouse_wheel_scroll (void)
{
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);
	GtkWidget *content = e_canvas_new ();
	GtkAdjustment *adjustment;
	MailListScroll *scroll;
	GdkEvent *event = gdk_event_new (GDK_SCROLL);
	GtkSettings *settings = gtk_widget_get_settings (window);
	gboolean animations, handled;
	gdouble initial, target, intermediate;

	g_object_get (settings, "gtk-enable-animations", &animations, NULL);
	g_object_set (settings, "gtk-enable-animations", TRUE, NULL);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (content), 0, 0, 300, 4000);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 400);
	gtk_container_add (GTK_CONTAINER (scrolled), content);
	gtk_container_add (GTK_CONTAINER (window), scrolled);
	mail_list_scroll_setup (scrolled);
	gtk_widget_show_all (window);
	scroll_test_wait (100);
	adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scrolled));
	scroll = g_object_get_data (G_OBJECT (scrolled), "mail-list-scroll");
	gdk_event_set_device (event, gdk_seat_get_pointer (gdk_display_get_default_seat (gtk_widget_get_display (window))));
	event->scroll.window = g_object_ref (gtk_widget_get_window (scrolled));
	event->scroll.direction = GDK_SCROLL_DOWN;

	initial = gtk_adjustment_get_value (adjustment);
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	g_assert_true (handled);
	g_assert_cmpfloat (gtk_adjustment_get_value (adjustment), ==, initial);
	target = scroll->target;
	g_assert_cmpfloat (target, >, initial);
	scroll_test_wait (50);
	intermediate = gtk_adjustment_get_value (adjustment);
	g_assert_cmpfloat (intermediate, >, initial);
	g_assert_cmpfloat (intermediate, <, target);
	scroll_test_wait (500);
	g_assert_cmpfloat_with_epsilon (gtk_adjustment_get_value (adjustment), target, 0.001);
	g_assert_cmpuint (scroll->tick_id, ==, 0);

	/* Repeated notches accumulate, but reversal discards the old destination. */
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	target = scroll->target;
	{
		gint64 frame_time = scroll->frame_time;
		gdouble velocity = scroll->velocity;

		g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
		g_assert_cmpint (scroll->frame_time, ==, frame_time);
		g_assert_cmpfloat (scroll->velocity, ==, velocity);
	}
	g_assert_cmpfloat (scroll->target, >, target);
	event->scroll.direction = GDK_SCROLL_UP;
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	g_assert_true (handled);
	g_assert_cmpfloat (scroll->target, <, gtk_adjustment_get_value (adjustment));

	gtk_adjustment_set_value (adjustment, 1000);
	g_assert_cmpuint (scroll->tick_id, ==, 0);
	scroll_test_wait (180);
	g_assert_cmpfloat (gtk_adjustment_get_value (adjustment), ==, 1000);

	event->scroll.direction = GDK_SCROLL_DOWN;
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	event->scroll.direction = GDK_SCROLL_SMOOTH;
	event->scroll.delta_y = 0.2;
	target = scroll->target;
	g_assert_true (mail_list_scroll_event (scrolled, &event->scroll, scroll));
	g_assert_cmpfloat (scroll->target, >, target);
	event->scroll.delta_y = 0.0;
	g_assert_true (mail_list_scroll_event (scrolled, &event->scroll, scroll));
	g_assert_cmpuint (scroll->tick_id, !=, 0);
	{
		gdouble delta;

		g_assert_false (mail_list_scroll_delta (&event->scroll, GDK_SOURCE_TOUCHPAD, &delta));
		g_assert_false (mail_list_scroll_delta (&event->scroll, GDK_SOURCE_TOUCHSCREEN, &delta));
	}
	event->scroll.delta_x = 0.5;
	g_assert_false (mail_list_scroll_event (scrolled, &event->scroll, scroll));
	g_assert_cmpuint (scroll->tick_id, ==, 0);
	event->scroll.delta_x = 0.0;

	event->scroll.direction = GDK_SCROLL_DOWN;
	event->scroll.state = GDK_SHIFT_MASK;
	g_assert_false (mail_list_scroll_event (scrolled, &event->scroll, scroll));
	event->scroll.state = 0;
	event->scroll.direction = GDK_SCROLL_LEFT;
	g_assert_false (mail_list_scroll_event (scrolled, &event->scroll, scroll));
	event->scroll.direction = GDK_SCROLL_DOWN;
	g_object_set (settings, "gtk-enable-animations", FALSE, NULL);
	g_assert_false (mail_list_scroll_event (scrolled, &event->scroll, scroll));
	g_assert_cmpuint (scroll->tick_id, ==, 0);
	g_object_set (settings, "gtk-enable-animations", TRUE, NULL);

	gtk_adjustment_set_value (adjustment, gtk_adjustment_get_upper (adjustment) -
		gtk_adjustment_get_page_size (adjustment) - 1);
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	g_assert_true (handled);
	scroll_test_wait (500);
	g_assert_cmpfloat_with_epsilon (gtk_adjustment_get_value (adjustment),
		gtk_adjustment_get_upper (adjustment) - gtk_adjustment_get_page_size (adjustment), 0.001);
	g_assert_false (mail_list_scroll_event (scrolled, &event->scroll, scroll));

	gtk_adjustment_set_value (adjustment, 1000);
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	gtk_adjustment_set_upper (adjustment, 5000);
	g_assert_cmpuint (scroll->tick_id, ==, 0);
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	g_assert_cmpuint (scroll->tick_id, !=, 0);
	mail_list_scroll_key_press (scrolled, NULL, scroll);
	g_assert_cmpuint (scroll->tick_id, ==, 0);
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	gtk_widget_hide (window);
	g_assert_cmpuint (scroll->tick_id, ==, 0);
	gtk_widget_show_all (window);
	g_signal_emit_by_name (scrolled, "scroll-event", event, &handled);
	g_assert_cmpuint (scroll->tick_id, !=, 0);
	gdk_event_free (event);
	gtk_widget_destroy (window);
	scroll_test_wait (180);
	g_object_set (settings, "gtk-enable-animations", animations, NULL);
}

typedef struct { CamelSession parent; } ThreadTestSession;
typedef CamelSessionClass ThreadTestSessionClass;
static GType thread_test_session_get_type (void);
G_DEFINE_TYPE (ThreadTestSession, thread_test_session, CAMEL_TYPE_SESSION)
static void thread_test_session_init (ThreadTestSession *session) {}
static void thread_test_session_class_init (ThreadTestSessionClass *class) {}

typedef struct { GObject parent; GNode *root; } ThreadOrderModel;
typedef GObjectClass ThreadOrderModelClass;
static GType thread_order_model_get_type (void);
static void thread_order_model_interface_init (ETreeModelInterface *interface);
G_DEFINE_TYPE_WITH_CODE (ThreadOrderModel, thread_order_model, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_TREE_MODEL, thread_order_model_interface_init))
static void thread_order_model_init (ThreadOrderModel *model) { model->root = g_node_new (NULL); }
static void thread_order_model_finalize (GObject *object)
{
	g_node_destroy (((ThreadOrderModel *) object)->root);
	G_OBJECT_CLASS (thread_order_model_parent_class)->finalize (object);
}
static void thread_order_model_class_init (ThreadOrderModelClass *class) { class->finalize = thread_order_model_finalize; }
static ETreePath thread_order_root (ETreeModel *model) { return ((ThreadOrderModel *) model)->root; }
static ETreePath thread_order_parent (ETreeModel *model, ETreePath path) { return ((GNode *) path)->parent; }
static ETreePath thread_order_child (ETreeModel *model, ETreePath path) { return ((GNode *) path)->children; }
static ETreePath thread_order_next (ETreeModel *model, ETreePath path) { return ((GNode *) path)->next; }
static gboolean thread_order_is_root (ETreeModel *model, ETreePath path) { return G_NODE_IS_ROOT ((GNode *) path); }
static gboolean thread_order_expandable (ETreeModel *model, ETreePath path) { return ((GNode *) path)->children != NULL; }
static gboolean thread_order_expanded (ETreeModel *model) { return TRUE; }
static guint thread_order_nodes (ETreeModel *model) { return g_node_n_nodes (((ThreadOrderModel *) model)->root, G_TRAVERSE_ALL) - 1; }
static guint thread_order_children (ETreeModel *model, ETreePath path) { return g_node_n_children (path); }
static guint thread_order_depth (ETreeModel *model, ETreePath path) { return g_node_depth (path); }
static gint thread_order_columns (ETreeModel *model) { return COL_LAST; }
static gpointer thread_order_value (ETreeModel *model, ETreePath path, gint column) { return ((GNode *) path)->data; }
static void thread_order_free_value (ETreeModel *model, gint column, gpointer value) {}
static gint thread_order_compare (gconstpointer first, gconstpointer second, gpointer data) { return e_int_compare (first, second); }
static void
thread_order_model_interface_init (ETreeModelInterface *interface)
{
	interface->get_root = thread_order_root;
	interface->get_parent = thread_order_parent;
	interface->get_first_child = thread_order_child;
	interface->get_next = thread_order_next;
	interface->is_root = thread_order_is_root;
	interface->is_expandable = thread_order_expandable;
	interface->get_expanded_default = thread_order_expanded;
	interface->get_n_nodes = thread_order_nodes;
	interface->get_n_children = thread_order_children;
	interface->depth = thread_order_depth;
	interface->column_count = thread_order_columns;
	interface->value_at = thread_order_value;
	interface->sort_value_at = thread_order_value;
	interface->free_value = thread_order_free_value;
}

static void
test_conversation_adapter_order (void)
{
	ThreadOrderModel *model = g_object_new (thread_order_model_get_type (), NULL);
	GNode *first = g_node_append_data (model->root, GINT_TO_POINTER (30));
	GNode *second = g_node_append_data (model->root, GINT_TO_POINTER (40));
	GError *error = NULL;
	gchar *path = g_build_filename (EVOLUTION_ETSPECDIR, "message-list.etspec", NULL);
	ETableSpecification *specification = e_table_specification_new (path, &error);
	ETableColumnSpecification *spec;
	ETableSortInfo *sort;
	ETableHeader *header = e_table_header_new ();
	ECell *cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	ETableCol *column;
	ETreeTableAdapter *adapter;
	const gint expected[][5] = { { 30, 20, 10, 40, 35 }, { 40, 35, 30, 20, 10 } };
	guint direction, row;
	g_assert_no_error (error);
	g_assert_nonnull (specification);
	g_free (path);
	g_node_append_data (first, GINT_TO_POINTER (20));
	g_node_append_data (first, GINT_TO_POINTER (10));
	g_node_append_data (second, GINT_TO_POINTER (35));
	spec = e_table_specification_get_column_by_model_col (specification, COL_SENT);
	g_assert_nonnull (spec);
	column = e_table_col_new (spec, "Synthetic date", NULL, cell, thread_order_compare);
	e_table_header_add_column (header, column, 0);
	sort = e_table_sort_info_new (specification);
	e_table_sort_info_sorting_set_nth (sort, 0, spec, GTK_SORT_ASCENDING);
	adapter = E_TREE_TABLE_ADAPTER (e_tree_table_adapter_new (E_TREE_MODEL (model), sort, header));
	e_tree_table_adapter_root_node_set_visible (adapter, FALSE);
	e_tree_table_adapter_set_sort_children_ascending (adapter, TRUE);
	e_tree_table_adapter_set_preserve_child_order (adapter, TRUE);
	for (direction = 0; direction < 2; direction++) {
		e_table_sort_info_sorting_set_nth (sort, 0, spec, direction == 0 ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);
		e_tree_model_node_data_changed (E_TREE_MODEL (model), first->children);
		while (g_main_context_iteration (NULL, FALSE));
		g_assert_cmpint (e_table_model_row_count (E_TABLE_MODEL (adapter)), ==, 5);
		for (row = 0; row < 5; row++) {
			GNode *node = e_tree_table_adapter_node_at_row (adapter, row);
			g_assert_cmpint (GPOINTER_TO_INT (node->data), ==, expected[direction][row]);
		}
	}
	g_object_unref (adapter);
	g_object_unref (sort);
	g_object_unref (column);
	g_object_unref (cell);
	g_object_unref (header);
	g_object_unref (specification);
	g_object_unref (model);
}

static void
test_unread_conversation_search (void)
{
	GError *error = NULL;
	gchar *directory = g_dir_make_tmp ("blueevolution-thread-search-XXXXXX", &error);
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	CamelFolder *folder;
	GPtrArray *matches = NULL;
	gchar *unread_uid = NULL;
	guint index;
	const gchar *query = "(match-threads \"all\" (match-all (not (system-flag \"Seen\"))))";
	g_assert_no_error (error);
	g_assert_cmpint (camel_init (directory, FALSE), ==, 0);
	camel_provider_init ();
	session = g_object_new (thread_test_session_get_type (), "user-data-dir", directory,
		"user-cache-dir", directory, NULL);
	service = camel_session_add_service (session, "synthetic-thread-test", "mbox", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (service);
	settings = camel_service_ref_settings (service);
	g_object_set (settings, "path", directory, NULL);
	g_object_unref (settings);
	g_assert_true (camel_service_connect_sync (service, NULL, &error));
	g_assert_no_error (error);
	folder = camel_store_get_folder_sync (CAMEL_STORE (service), "INBOX", CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (folder);
	for (index = 0; index < 4; index++) {
		CamelMimeMessage *message = camel_mime_message_new ();
		CamelMessageInfo *info = camel_message_info_new (NULL);
		gchar *message_id = g_strdup_printf ("synthetic-%u@example.invalid", index);
		gchar *uid = NULL;
		camel_mime_message_set_message_id (message, message_id);
		camel_mime_message_set_subject (message, index < 3 ? "Synthetic conversation" : "Unrelated synthetic message");
		camel_mime_message_set_date (message, 1000 + index, 0);
		camel_mime_part_set_content (CAMEL_MIME_PART (message), "Synthetic test body", -1, "text/plain");
		if (index == 1 || index == 2) {
			camel_medium_set_header (CAMEL_MEDIUM (message), "References", index == 1 ?
				"<synthetic-0@example.invalid>" : "<synthetic-0@example.invalid> <synthetic-1@example.invalid>");
			camel_medium_set_header (CAMEL_MEDIUM (message), "In-Reply-To", index == 1 ?
				"<synthetic-0@example.invalid>" : "<synthetic-1@example.invalid>");
		}
		camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, index == 1 ? 0 : CAMEL_MESSAGE_SEEN);
		g_assert_true (camel_folder_append_message_sync (folder, message, info, &uid, NULL, &error));
		g_assert_no_error (error);
		if (index == 1) unread_uid = uid; else g_free (uid);
		g_free (message_id);
		g_object_unref (message);
		g_object_unref (info);
	}
	g_assert_true (camel_folder_search_sync (folder, query, &matches, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (matches->len, ==, 3);
	{
		CamelFolderThread *thread = camel_folder_thread_new (folder, matches, CAMEL_FOLDER_THREAD_FLAG_NONE);
		GPtrArray *messages = g_ptr_array_new ();
		message_list_collect_thread_messages (camel_folder_thread_get_tree (thread), messages);
		g_assert_cmpuint (messages->len, ==, 3);
		g_ptr_array_sort (messages, message_list_thread_message_compare);
		for (index = 0; index < messages->len; index++)
			g_assert_cmpint (camel_message_info_get_date_sent (g_ptr_array_index (messages, index)), ==, 1002 - index);
		g_ptr_array_unref (messages);
		g_object_unref (thread);
	}
	g_ptr_array_unref (matches);
	matches = NULL;
	g_assert_true (camel_folder_search_sync (folder,
		"(match-threads \"all\" (and (match-all (header-contains \"subject\" \"Synthetic conversation\")) (match-all (not (system-flag \"Seen\")))))",
		&matches, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (matches->len, ==, 3);
	g_ptr_array_unref (matches);
	matches = NULL;
	camel_folder_set_message_flags (folder, unread_uid, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_assert_true (camel_folder_search_sync (folder, query, &matches, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (matches->len, ==, 0);
	g_ptr_array_unref (matches);
	g_free (unread_uid);
	g_object_unref (folder);
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
test_reply_preserves_unread_default (void)
{
	GSettings *settings = g_settings_new ("org.gnome.evolution.mail");
	GVariant *value = g_settings_get_default_value (settings, "composer-mark-read-on-reply");
	g_assert_false (g_variant_get_boolean (value));
	g_variant_unref (value);
	g_object_unref (settings);
}

static void
test_note_replacement (void)
{
	GError *error = NULL;
	gchar *directory = g_dir_make_tmp ("blueevolution-note-test-XXXXXX", &error);
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	CamelFolder *folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	gchar *uid = NULL;
	guint step;

	g_assert_no_error (error);
	g_assert_cmpint (camel_init (directory, FALSE), ==, 0);
	camel_provider_init ();
	session = g_object_new (thread_test_session_get_type (),
		"user-data-dir", directory, "user-cache-dir", directory, NULL);
	service = camel_session_add_service (session, "synthetic-note-test", "mbox", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	settings = camel_service_ref_settings (service);
	g_object_set (settings, "path", directory, NULL);
	g_object_unref (settings);
	g_assert_true (camel_service_connect_sync (service, NULL, &error));
	g_assert_no_error (error);
	folder = camel_store_get_folder_sync (CAMEL_STORE (service), "INBOX", CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	message = camel_mime_message_new ();
	camel_mime_message_set_message_id (message, "synthetic-note@example.invalid");
	camel_mime_message_set_subject (message, "Synthetic note replacement");
	camel_mime_part_set_content (CAMEL_MIME_PART (message), "Synthetic body", -1, "text/plain");
	info = camel_message_info_new (NULL);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	g_assert_true (camel_folder_append_message_sync (folder, message, info, &uid, NULL, &error));
	g_assert_no_error (error);
	g_object_unref (info);

	for (step = 0; step < 3; step++) {
		CamelMessageInfo *original = camel_folder_get_message_info (folder, uid);
		CamelMessageInfo *clone = camel_message_info_clone (original, NULL);
		CamelMessageInfo *replacement;
		CamelFolderChangeInfo *changes = camel_folder_change_info_new ();
		gchar *new_uid = NULL;
		gchar *found_uid;

		camel_message_info_set_user_flag (clone, E_MAIL_NOTES_USER_FLAG, step < 2);
		camel_folder_freeze (folder);
		g_assert_true (camel_folder_append_message_sync (folder, message, clone, &new_uid, NULL, &error));
		g_assert_no_error (error);
		camel_message_info_set_flags (original, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
		camel_folder_thaw (folder);
		camel_folder_change_info_add_uid (changes, new_uid);
		camel_folder_change_info_change_uid (changes, uid);
		replacement = camel_folder_get_message_info (folder, new_uid);
		g_assert_nonnull (replacement);
		g_assert_true (message_list_is_note_replacement (original, replacement));
		g_assert_cmpint (camel_message_info_get_user_flag (replacement, E_MAIL_NOTES_USER_FLAG), ==, step < 2);
		found_uid = message_list_find_note_replacement (folder, changes, uid);
		g_assert_cmpstr (found_uid, ==, new_uid);
		g_assert_true (message_list_keep_displayed_message (NULL,
			camel_message_info_get_flags (replacement), FALSE, FALSE, TRUE));
		g_assert_true (message_list_keep_displayed_message (
			"(match-all (not (system-flag \"Seen\")))",
			camel_message_info_get_flags (replacement), FALSE, FALSE, TRUE));
		{
			MessageList selection = { 0 };
			MessageListPrivate selection_priv = { 0 };
			const gchar *searches[] = { NULL, "(match-all (not (system-flag \"Seen\")))" };
			gchar *selected_uid;
			guint search_index;

			selection.priv = &selection_priv;
			selection.cursor_uid = g_strdup (uid);
			g_mutex_init (&selection_priv.regen_lock);
			selected_uid = message_list_retarget_note_replacement (&selection, folder, changes);
			g_assert_cmpstr (selected_uid, ==, new_uid);
			g_assert_cmpstr (selection.cursor_uid, ==, new_uid);
			g_assert_cmpstr (selection_priv.automatically_seen_uid, ==, new_uid);
			for (search_index = 0; search_index < G_N_ELEMENTS (searches); search_index++) {
				GPtrArray *results = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);

				selection.search = (gchar *) searches[search_index];
				message_list_regen_tweak_search_results (&selection, results, folder, TRUE, FALSE, FALSE);
				g_assert_cmpuint (results->len, ==, 1);
				g_assert_cmpstr (results->pdata[0], ==, new_uid);
				g_ptr_array_unref (results);
			}
			g_free (selected_uid);
			g_free (selection.cursor_uid);
			g_free (selection_priv.automatically_seen_uid);
			g_mutex_clear (&selection_priv.regen_lock);
		}
		camel_message_info_set_message_id (clone, 0);
		g_assert_false (message_list_is_note_replacement (original, clone));
		camel_message_info_set_flags (replacement, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
		g_assert_false (message_list_is_note_replacement (original, replacement));
		camel_message_info_set_flags (replacement, CAMEL_MESSAGE_DELETED, 0);
		g_free (found_uid);
		g_free (uid);
		uid = new_uid;
		g_object_unref (replacement);
		g_object_unref (original);
		g_object_unref (clone);
		camel_folder_change_info_free (changes);
	}

	g_free (uid);
	g_object_unref (message);
	g_object_unref (folder);
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
test_unread_filter_refresh (void)
{
	const gchar *unread = "(match-all (not (system-flag \"Seen\")))";
	const gchar *combined = "(and (match-all (header-contains \"subject\" \"Synthetic\")) (match-all (not (system-flag \"Seen\"))))";
	g_assert_true (message_list_search_hides_read (unread));
	g_assert_true (message_list_search_hides_read (combined));
	g_assert_true (message_list_search_hides_read ("(match-all (not (system-flag \"seen\")))"));
	g_assert_false (message_list_search_hides_read (NULL));
	g_assert_false (message_list_search_hides_read (" "));
	g_assert_false (message_list_search_hides_read ("(match-all (system-flag \"Seen\"))"));
	g_assert_false (message_list_show_deleted_for_search (unread, TRUE));
	g_assert_false (message_list_show_deleted_for_search (combined, TRUE));
	g_assert_false (message_list_show_deleted_for_search (unread, FALSE));
	g_assert_true (message_list_show_deleted_for_search (NULL, TRUE));
	g_assert_false (message_list_show_deleted_for_search (NULL, FALSE));
	g_assert_true (message_list_keep_displayed_message (unread, 0, FALSE, FALSE, FALSE));
	g_assert_false (message_list_keep_displayed_message (unread, CAMEL_MESSAGE_SEEN, FALSE, FALSE, FALSE));
	g_assert_true (message_list_keep_displayed_message (unread, CAMEL_MESSAGE_SEEN, FALSE, FALSE, TRUE));
	g_assert_true (message_list_keep_displayed_message (combined, CAMEL_MESSAGE_SEEN, TRUE, TRUE, TRUE));
	g_assert_false (message_list_keep_displayed_message (combined, CAMEL_MESSAGE_SEEN, TRUE, TRUE, FALSE));
	g_assert_false (message_list_keep_displayed_message (unread, CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN, TRUE, TRUE, TRUE));
	g_assert_false (message_list_keep_displayed_message (unread, CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_SEEN, FALSE, FALSE, TRUE));
	g_assert_false (message_list_keep_displayed_message (unread, CAMEL_MESSAGE_DELETED, TRUE, TRUE, FALSE));
	g_assert_false (message_list_keep_displayed_message (combined, CAMEL_MESSAGE_DELETED, TRUE, TRUE, FALSE));
	g_assert_true (message_list_keep_displayed_message (NULL, CAMEL_MESSAGE_SEEN, FALSE, FALSE, FALSE));
	g_assert_false (message_list_keep_displayed_message (NULL, CAMEL_MESSAGE_DELETED, FALSE, FALSE, FALSE));
	g_assert_false (message_list_keep_displayed_message (NULL, CAMEL_MESSAGE_JUNK, FALSE, FALSE, FALSE));
	g_assert_true (message_list_keep_displayed_message (NULL, CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_JUNK, TRUE, TRUE, FALSE));
}

static void
test_collapsed_thread_latest_message (void)
{
	CamelMessageInfo *first = camel_message_info_new (NULL);
	CamelMessageInfo *reply = camel_message_info_new (NULL);
	CamelMessageInfo *latest = camel_message_info_new (NULL);
	GNode *root = g_node_new (first);
	GNode *child = g_node_append_data (root, reply);
	camel_message_info_set_uid (first, "first");
	camel_message_info_set_uid (reply, "reply");
	camel_message_info_set_uid (latest, "latest");
	camel_message_info_set_date_received (first, 100);
	camel_message_info_set_date_received (reply, 200);
	camel_message_info_set_date_received (latest, 300);
	g_node_append_data (child, latest);
	g_assert_true (message_list_thread_message_info (root, TRUE) == latest);
	g_assert_true (message_list_thread_message_info (root, FALSE) == first);
	g_assert_true (message_list_thread_message_info (child, TRUE) == latest);
	g_assert_true (message_list_thread_message_info (child, FALSE) == reply);
	camel_message_info_set_date_received (latest, 0);
	camel_message_info_set_date_sent (latest, 400);
	g_assert_true (message_list_thread_message_info (root, TRUE) == latest);
	g_node_destroy (root);
	g_object_unref (first);
	g_object_unref (reply);
	g_object_unref (latest);
}

typedef struct { GObject parent; } WidthModel;
typedef GObjectClass WidthModelClass;
static GType width_model_get_type (void);
static void width_model_interface_init (ETableModelInterface *interface);
G_DEFINE_TYPE_WITH_CODE (WidthModel, width_model, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_TABLE_MODEL, width_model_interface_init))

static gint width_model_rows (ETableModel *model) { return 1; }
static gint width_model_columns (ETableModel *model) { return 2; }
static void width_model_init (WidthModel *model) {}
static void width_model_class_init (WidthModelClass *class) {}

static void
width_model_interface_init (ETableModelInterface *interface)
{
	interface->row_count = width_model_rows;
	interface->column_count = width_model_columns;
}

typedef struct { ECell parent; gint drawn_width; gint drawn_y1; gint drawn_y2; } WidthCell;
typedef ECellClass WidthCellClass;
static GType width_cell_get_type (void);
G_DEFINE_TYPE (WidthCell, width_cell, E_TYPE_CELL)

static gint
width_cell_measure (ECellView *view, gint model_column, gint view_column, gint row)
{
	return 90;
}

static void
width_cell_draw (ECellView *view, cairo_t *context, gint model_column, gint view_column,
                 gint row, ECellFlags flags, gint x1, gint y1, gint x2, gint y2)
{
	((WidthCell *) view->ecell)->drawn_width = x2 - x1;
	((WidthCell *) view->ecell)->drawn_y1 = y1;
	((WidthCell *) view->ecell)->drawn_y2 = y2;
}

static void width_cell_init (WidthCell *cell) {}

static void
width_cell_class_init (WidthCellClass *class)
{
	class->draw = width_cell_draw;
	class->height = width_cell_measure;
	class->max_width_by_row = width_cell_measure;
}

static void
test_widths (void)
{
	ECellHbox *box = E_CELL_HBOX (e_cell_hbox_new ());
	WidthCell *sender = g_object_new (width_cell_get_type (), NULL);
	WidthCell *date = g_object_new (width_cell_get_type (), NULL);
	ETableModel *model = g_object_new (width_model_get_type (), NULL);
	ECellView sender_view = { .ecell = E_CELL (sender), .e_table_model = model };
	ECellView date_view = { .ecell = E_CELL (date), .e_table_model = model };
	ECellView *children[] = { &sender_view, &date_view };
	ECellHboxView view = { 0 };
	cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *context = cairo_create (surface);
	const gint widths[] = { 220, 320, 520 };
	guint index;

	e_cell_hbox_append (box, E_CELL (sender), 0, 100);
	e_cell_hbox_append_natural (box, E_CELL (date), 1);
	view.cell_view.ecell = E_CELL (box);
	view.cell_view.e_table_model = model;
	view.subcell_view_count = 2;
	view.subcell_views = children;
	view.model_cols = box->model_cols;
	view.def_size_cols = box->def_size_cols;
	for (index = 0; index < G_N_ELEMENTS (widths); index++) {
		e_cell_draw ((ECellView *) &view, context, 0, 0, 0, 0, 0, 0, widths[index], 20);
		g_assert_cmpint (date->drawn_width, ==, 90);
		g_assert_cmpint (sender->drawn_width, ==, widths[index] - 90);
	}
	box->def_size_cols[0] = 60;
	box->def_size_cols[1] = 40;
	e_cell_draw ((ECellView *) &view, context, 0, 0, 0, 0, 0, 0, 300, 20);
	g_assert_cmpint (sender->drawn_width, ==, 180);
	g_assert_cmpint (date->drawn_width, ==, 120);
	cairo_destroy (context);
	cairo_surface_destroy (surface);
	g_object_unref (box);
	g_object_unref (sender);
	g_object_unref (date);
	g_object_unref (model);
}

static void
test_date_headings_only_for_top_level_rows (void)
{
	GNode *tree = g_node_new (NULL);
	GNode *thread = g_node_new (NULL);
	GNode *reply = g_node_new (NULL);

	g_node_append (tree, thread);
	g_node_append (thread, reply);
	g_assert_true (mail_list_is_top_level_row (thread));
	g_assert_false (mail_list_is_top_level_row (reply));
	g_assert_cmpint (mail_list_node_indent (thread), ==, 0);
	g_assert_cmpint (mail_list_node_indent (reply), ==, 20);
	{
		GNode *nested = g_node_append_data (reply, NULL);
		g_assert_cmpint (mail_list_node_indent (nested), ==, 20);
		for (guint depth = 0; depth < 8; depth++)
			nested = g_node_append_data (nested, NULL);
		g_assert_cmpint (mail_list_node_indent (nested), ==, 20);
	}
	g_node_destroy (tree);
}

typedef struct { GObject parent; gboolean unread; guint32 status_flags; } RowModel;
typedef GObjectClass RowModelClass;
static GType row_model_get_type (void);
static void row_model_interface_init (ETableModelInterface *interface);
G_DEFINE_TYPE_WITH_CODE (RowModel, row_model, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_TABLE_MODEL, row_model_interface_init))

static void row_model_init (RowModel *model) { model->unread = TRUE; }
static void row_model_class_init (RowModelClass *class) {}
static gint row_model_columns (ETableModel *model) { return 3; }
static gpointer row_model_value (ETableModel *model, gint column, gint row)
{
	if (column == 2)
		return GUINT_TO_POINTER (((RowModel *) model)->status_flags);
	return column == 0 ? g_strdup ("Synthetic Sender") : GINT_TO_POINTER (((RowModel *) model)->unread);
}
static void row_model_free_value (ETableModel *model, gint column, gpointer value)
{
	if (column == 0)
		g_free (value);
}
static void row_model_interface_init (ETableModelInterface *interface)
{
	interface->row_count = width_model_rows;
	interface->column_count = row_model_columns;
	interface->value_at = row_model_value;
	interface->free_value = row_model_free_value;
}

static guint32
test_surface_pixel (cairo_surface_t *surface, gint x, gint y)
{
	guint32 pixel;

	cairo_surface_flush (surface);
	memcpy (&pixel, cairo_image_surface_get_data (surface) + y * cairo_image_surface_get_stride (surface) + x * 4, sizeof (pixel));
	return pixel;
}

static void
test_message_status_icons (void)
{
	const guint32 cases[] = { 0, CAMEL_MESSAGE_ANSWERED, CAMEL_MESSAGE_ANSWERED_ALL,
		CAMEL_MESSAGE_FORWARDED, CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_FORWARDED };
	const gchar *palettes[] = { "* { color: #202020; }", "* { color: #eeeeee; }" };
	GtkWidget *canvas = gnome_canvas_new ();
	GtkCssProvider *style = gtk_css_provider_new ();
	RowModel *model = g_object_new (row_model_get_type (), NULL);
	ECell *cell = e_mail_list_status_cell_new ();
	ECellView *view;
	guint palette, index;
	gtk_icon_theme_prepend_search_path (gtk_icon_theme_get_default (), EVOLUTION_ICONDIR);
	g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", "BlueEvolution", NULL);
	g_object_ref_sink (canvas);
	view = e_cell_new_view (cell, E_TABLE_MODEL (model), gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (canvas)), E_TYPE_TABLE_ITEM, NULL));
	gtk_style_context_add_provider (gtk_widget_get_style_context (canvas), GTK_STYLE_PROVIDER (style), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (palette = 0; palette < G_N_ELEMENTS (palettes); palette++) {
		gtk_css_provider_load_from_data (style, palettes[palette], -1, NULL);
		for (index = 0; index < G_N_ELEMENTS (cases); index++) {
			cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 40, 20);
			cairo_t *context = cairo_create (surface);
			guint left = 0, right = 0, brightness = 0, opaque = 0;
			gint column, row;
			model->status_flags = cases[index];
			e_cell_draw (view, context, 2, 0, 0, 0, 0, 0, 40, 20);
			g_assert_cmpint (e_cell_max_width (view, 2, 0), ==, 40);
			for (row = 0; row < 20; row++) {
				for (column = 0; column < 40; column++) {
					guint32 pixel = test_surface_pixel (surface, column, row);
					if (pixel >> 24) {
						if (column < 20) left++; else right++;
					}
					if ((pixel >> 24) == 255) { brightness += pixel & 255; opaque++; }
				}
			}
			if (index == 0)
				g_assert_cmpuint (right, ==, 0);
			else
				g_assert_cmpuint (right, >, 0);
			if (index == 4) g_assert_cmpuint (left, >, 0); else g_assert_cmpuint (left, ==, 0);
			if (opaque) g_assert_cmpuint (brightness / opaque, ==, palette == 0 ? 32 : 238);
			cairo_destroy (context);
			cairo_surface_destroy (surface);
		}
	}
	e_cell_kill_view (view);
	g_object_unref (cell);
	g_object_unref (model);
	g_object_unref (style);
	gtk_widget_destroy (canvas);
	g_object_unref (canvas);
}

static void
test_message_status_header (void)
{
	GString *html = g_string_new (NULL);
	format_message_status_icons (html, 0);
	g_assert_cmpstr (html->str, ==, "");
	format_message_status_icons (html, CAMEL_MESSAGE_SEEN);
	g_assert_cmpstr (html->str, ==, "");
	format_message_status_icons (html, CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_FORWARDED);
	g_assert_nonnull (strstr (html->str, "gtk-stock://mail-reply-sender"));
	g_assert_nonnull (strstr (html->str, "gtk-stock://mail-forward"));
	g_assert_nonnull (strstr (html->str, _("You replied to this message.")));
	g_assert_nonnull (strstr (html->str, _("You forwarded this message.")));
	g_assert_nonnull (strstr (html->str, "color-scheme=light"));
	g_assert_nonnull (strstr (html->str, "color-scheme=dark"));
	g_string_truncate (html, 0);
	format_message_status_icons (html, CAMEL_MESSAGE_ANSWERED_ALL);
	g_assert_nonnull (strstr (html->str, "gtk-stock://mail-reply-all"));
	g_assert_null (strstr (html->str, "gtk-stock://mail-forward"));
	g_assert_nonnull (strstr (html->str, _("You replied to all recipients of this message.")));
	g_string_free (html, TRUE);
	g_assert_true (message_list_value_is_empty (NULL, COL_REPLY_STATUS, NULL));
	message_list_free_value (NULL, COL_REPLY_STATUS, GUINT_TO_POINTER (CAMEL_MESSAGE_ANSWERED));
}

static void
test_unread_row_background (void)
{
	GtkWidget *canvas = gnome_canvas_new ();
	GnomeCanvasItem *item;
	GtkCssProvider *provider = gtk_css_provider_new ();
	RowModel *model = g_object_new (row_model_get_type (), NULL);
	WidthCell *content = g_object_new (width_cell_get_type (), NULL);
	ECell *cell = e_mail_list_cell_new (E_CELL (content), 0, 2, 1);
	ECellView content_view = { .ecell = E_CELL (content), .e_table_model = E_TABLE_MODEL (model) };
	EMailListCellView view = { 0 };
	const gchar *palettes[] = {
		"@define-color theme_base_color #2d2d2d; @define-color theme_text_color #eeeeec;",
		"@define-color theme_base_color #ffffff; @define-color theme_text_color #202020;"
	};
	guint index;

	g_object_ref_sink (canvas);
	item = gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (canvas)), E_TYPE_TABLE_ITEM, NULL);
	view.parent.ecell = cell;
	view.parent.e_table_model = E_TABLE_MODEL (model);
	view.parent.e_table_item_view = item;
	view.content = &content_view;
	gtk_style_context_add_provider (gtk_widget_get_style_context (canvas), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (index = 0; index < G_N_ELEMENTS (palettes); index++) {
		cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 320, 90);
		cairo_t *context = cairo_create (surface);
		guint32 unread_pixel, selected_pixel;

		gtk_css_provider_load_from_data (provider, palettes[index], -1, NULL);
		e_cell_draw ((ECellView *) &view, context, 0, 0, 0, 0, 0, 0, 320, 90);
		g_assert_cmpint (content->drawn_y1, ==, 12);
		g_assert_cmpint (content->drawn_y2, ==, 78);
		g_assert_cmpint (e_cell_height ((ECellView *) &view, 0, 0, 0), ==, 114);
		unread_pixel = test_surface_pixel (surface, 80, 5);
		g_assert_cmpuint (unread_pixel >> 24, ==, 255);
		if (index == 0) {
			g_assert_cmpuint (unread_pixel & 255, >, 45);
			g_assert_cmpuint (unread_pixel & 255, <, 75);
		} else {
			g_assert_cmpuint (unread_pixel & 255, >, 235);
			g_assert_cmpuint (unread_pixel & 255, <, 255);
		}
		cairo_set_source_rgb (context, 0.1, 0.4, 0.7);
		cairo_paint (context);
		selected_pixel = test_surface_pixel (surface, 80, 5);
		e_cell_draw ((ECellView *) &view, context, 0, 0, 0, E_CELL_SELECTED, 0, 0, 320, 90);
		g_assert_cmphex (test_surface_pixel (surface, 80, 5), ==, selected_pixel);
		cairo_destroy (context);
		cairo_surface_destroy (surface);
	}
	g_object_unref (cell);
	g_object_unref (content);
	g_object_unref (model);
	g_object_unref (provider);
	gtk_widget_destroy (canvas);
	g_object_unref (canvas);
}

static void
test_expander_placement (void)
{
	GdkRectangle left, right;

	mail_list_expander_bounds (320, 90, 0, FALSE, &left);
	mail_list_expander_bounds (320, 122, 32, TRUE, &right);
	g_assert_cmpint (left.x, ==, 12);
	g_assert_cmpint (right.x, ==, 320 - 36);
	g_assert_cmpint (right.y - left.y, ==, 32);
	g_assert_cmpint (left.width, ==, 24);
	g_assert_cmpint (left.height, ==, 24);
	g_assert_cmpint (left.y + left.height, <=, 90);
	g_assert_cmpint (left.y, >, 90 / 2 + 4);
}

static void
test_attachment_symbolic_foreground (void)
{
	const gchar *icons[] = { NULL, "mail-attachment-symbolic" };
	const gchar *palettes[] = { "* { color: #202020; }", "* { color: #eeeeee; }", "* { color: #202020; }" };
	GtkWidget *canvas = gnome_canvas_new ();
	GtkCssProvider *provider = gtk_css_provider_new ();
	RowModel *model = g_object_new (row_model_get_type (), NULL);
	ECell *cell = e_cell_toggle_new (icons, G_N_ELEMENTS (icons));
	ECellView *view;
	guint index;

	g_object_ref_sink (canvas);
	view = e_cell_new_view (cell, E_TABLE_MODEL (model), gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (canvas)), E_TYPE_TABLE_ITEM, NULL));
	gtk_style_context_add_provider (gtk_widget_get_style_context (canvas), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (index = 0; index < G_N_ELEMENTS (palettes); index++) {
		cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 32, 24);
		cairo_t *context = cairo_create (surface);
		guint pixels = 0, brightness = 0;
		gint pixel_x, pixel_y;

		gtk_css_provider_load_from_data (provider, palettes[index], -1, NULL);
		e_cell_draw (view, context, 1, 0, 0, 0, 0, 0, 32, 24);
		for (pixel_y = 0; pixel_y < 24; pixel_y++) {
			for (pixel_x = 0; pixel_x < 32; pixel_x++) {
				guint32 pixel = test_surface_pixel (surface, pixel_x, pixel_y);

				if ((pixel >> 24) > 200) {
					brightness += (pixel & 255) * 255 / (pixel >> 24);
					pixels++;
				}
			}
		}
		g_assert_cmpuint (pixels, >, 5);
		if (index == 1)
			g_assert_cmpuint (brightness / pixels, >, 220);
		else
			g_assert_cmpuint (brightness / pixels, <, 50);
		cairo_destroy (context);
		cairo_surface_destroy (surface);
	}
	e_cell_kill_view (view);
	g_object_unref (cell);
	g_object_unref (model);
	g_object_unref (provider);
	gtk_widget_destroy (canvas);
	g_object_unref (canvas);
}

static void
test_older_dates_use_abbreviated_weekday (void)
{
	GTimeZone *zone = g_time_zone_new_identifier ("Europe/Zurich");
	GDateTime *now = g_date_time_new (zone, 2026, 10, 21, 15, 0, 0);
	GDateTime *date = g_date_time_new (zone, 2026, 10, 14, 15, 0, 0);
	gchar *weekday = g_date_time_format (date, "%a");
	gchar *short_date = g_date_time_format (date, "%x");
	gchar *expected = g_strdup_printf ("%s, %s", weekday, short_date);
	gchar *actual = mail_list_compact_date (g_date_time_to_unix (date), now);

	g_assert_cmpstr (actual, ==, expected);
	g_free (actual);
	g_free (expected);
	g_free (short_date);
	g_free (weekday);
	g_date_time_unref (date);
	g_date_time_unref (now);
	g_time_zone_unref (zone);
}

static void
test_attachment_stream_foreground (void)
{
	GIcon *icon = g_themed_icon_new ("mail-attachment-symbolic");
	const gchar *colors[] = { "#202020", "#eeeeee", "#202020" };
	guint index;

	for (index = 0; index < G_N_ELEMENTS (colors); index++) {
		GOutputStream *output = g_memory_output_stream_new_resizable ();
		GInputStream *input;
		GBytes *bytes;
		GdkPixbuf *pixbuf;
		GdkRGBA foreground;
		gchar *mime_type = NULL;
		guint pixels = 0, brightness = 0;
		gint pixel_x, pixel_y;

		gdk_rgba_parse (&foreground, colors[index]);
		save_gicon_to_stream (icon, 32, &foreground, output, &mime_type);
		g_assert_cmpstr (mime_type, ==, "image/png");
		g_assert_true (g_output_stream_close (output, NULL, NULL));
		bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
		input = g_memory_input_stream_new_from_bytes (bytes);
		pixbuf = gdk_pixbuf_new_from_stream (input, NULL, NULL);
		g_assert_nonnull (pixbuf);
		g_assert_cmpint (gdk_pixbuf_get_width (pixbuf), ==, 32);
		for (pixel_y = 0; pixel_y < gdk_pixbuf_get_height (pixbuf); pixel_y++) {
			for (pixel_x = 0; pixel_x < gdk_pixbuf_get_width (pixbuf); pixel_x++) {
				const guchar *pixel = gdk_pixbuf_read_pixels (pixbuf) +
					pixel_y * gdk_pixbuf_get_rowstride (pixbuf) + pixel_x * gdk_pixbuf_get_n_channels (pixbuf);

				if (!gdk_pixbuf_get_has_alpha (pixbuf) || pixel[3] > 200) {
					brightness += pixel[0];
					pixels++;
				}
			}
		}
		g_assert_cmpuint (pixels, >, 5);
		if (index == 1)
			g_assert_cmpuint (brightness / pixels, >, 220);
		else
			g_assert_cmpuint (brightness / pixels, <, 50);
		g_object_unref (pixbuf);
		g_object_unref (input);
		g_bytes_unref (bytes);
		g_object_unref (output);
		g_free (mime_type);
	}
	g_object_unref (icon);
}

static void
test_attachment_stream_preserves_image (void)
{
	GdkPixbuf *original = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
	GdkPixbuf *loaded;
	GOutputStream *output = g_memory_output_stream_new_resizable ();
	GInputStream *input;
	GBytes *bytes, *image_bytes;
	GIcon *icon;
	GdkRGBA foreground = { 1, 1, 1, 1 };
	gchar *buffer, *mime_type = NULL;
	gsize length;
	const guchar *pixel;

	gdk_pixbuf_fill (original, 0x118833ff);
	g_assert_true (gdk_pixbuf_save_to_buffer (original, &buffer, &length, "png", NULL, NULL));
	image_bytes = g_bytes_new_take (buffer, length);
	icon = g_bytes_icon_new (image_bytes);
	save_gicon_to_stream (icon, 16, &foreground, output, &mime_type);
	g_assert_cmpstr (mime_type, ==, "image/png");
	g_assert_true (g_output_stream_close (output, NULL, NULL));
	bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
	input = g_memory_input_stream_new_from_bytes (bytes);
	loaded = gdk_pixbuf_new_from_stream (input, NULL, NULL);
	g_assert_nonnull (loaded);
	pixel = gdk_pixbuf_read_pixels (loaded);
	g_assert_cmpuint (pixel[0], ==, 0x11);
	g_assert_cmpuint (pixel[1], ==, 0x88);
	g_assert_cmpuint (pixel[2], ==, 0x33);
	g_object_unref (loaded);
	g_object_unref (input);
	g_bytes_unref (bytes);
	g_object_unref (icon);
	g_bytes_unref (image_bytes);
	g_object_unref (output);
	g_object_unref (original);
	g_free (mime_type);
}

static void
test_thread_expand_default_closed (void)
{
	GSettings *settings = e_util_ref_settings ("org.gnome.evolution.mail");
	GVariant *default_value = g_settings_get_default_value (settings, "thread-expand");

	g_assert_nonnull (default_value);
	g_assert_false (g_variant_get_boolean (default_value));
	g_variant_unref (default_value);
	g_object_unref (settings);
}

static void
check_group (GTimeZone *zone, GDateTime *now, gint year, gint month, gint day, gint expected)
{
	GDateTime *date = g_date_time_new (zone, year, month, day, 12, 0, 0);

	g_assert_cmpint (mail_list_date_group (g_date_time_to_unix (date), now), ==, expected);
	g_date_time_unref (date);
}

static void
test_dates (void)
{
	GTimeZone *zone = g_time_zone_new_identifier ("Europe/Zurich");
	GDateTime *now = g_date_time_new (zone, 2026, 10, 21, 15, 0, 0);
	GDateTime *boundary, *previous;
	GSettings *settings = e_util_ref_settings ("org.gnome.evolution.calendar");
	gchar *text;
	gchar *expected;

	check_group (zone, now, 2026, 10, 22, 0);
	check_group (zone, now, 2026, 10, 21, 1);
	check_group (zone, now, 2026, 10, 20, 2);
	check_group (zone, now, 2026, 10, 19, 3);
	check_group (zone, now, 2026, 10, 15, 4);
	check_group (zone, now, 2026, 10, 2, 5);
	check_group (zone, now, 2026, 9, 15, 6);
	check_group (zone, now, 2026, 8, 15, 7);
	g_assert_cmpint (mail_list_date_group (0, now), ==, 8);
	g_settings_set_boolean (settings, "use-24hour-format", TRUE);
	text = mail_list_compact_date (g_date_time_to_unix (now), now);
	expected = g_strdup_printf ("%s, 15:00", _("Today"));
	g_assert_cmpstr (text, ==, expected);
	g_free (expected);
	g_free (text);
	previous = g_date_time_add_days (now, -1);
	text = mail_list_compact_date (g_date_time_to_unix (previous), now);
	expected = g_strdup_printf ("%s, 15:00", _("Yesterday"));
	g_assert_cmpstr (text, ==, expected);
	g_free (expected);
	g_free (text);
	g_date_time_unref (previous);
	g_settings_set_boolean (settings, "use-24hour-format", FALSE);
	text = mail_list_compact_date (g_date_time_to_unix (now), now);
	g_assert_nonnull (strstr (text, "03:00"));
	g_assert_null (strstr (text, "15:00"));
	g_free (text);
	g_object_unref (settings);
	g_date_time_unref (now);
	boundary = g_date_time_new (zone, 2026, 3, 30, 0, 30, 0);
	previous = g_date_time_new (zone, 2026, 3, 29, 23, 30, 0);
	g_assert_cmpint (mail_list_date_group (g_date_time_to_unix (previous), boundary), ==, 2);
	g_date_time_unref (previous);
	g_date_time_unref (boundary);
	boundary = g_date_time_new (zone, 2026, 10, 26, 0, 30, 0);
	previous = g_date_time_new (zone, 2026, 10, 25, 23, 30, 0);
	g_assert_cmpint (mail_list_date_group (g_date_time_to_unix (previous), boundary), ==, 2);
	g_date_time_unref (previous);
	g_date_time_unref (boundary);
	boundary = g_date_time_new (zone, 2026, 1, 15, 12, 0, 0);
	check_group (zone, boundary, 2025, 12, 20, 6);
	g_date_time_unref (boundary);
	boundary = g_date_time_new (zone, 2024, 3, 1, 12, 0, 0);
	check_group (zone, boundary, 2024, 2, 29, 2);
	g_date_time_unref (boundary);
	g_time_zone_unref (zone);
}

int
main (int argument_count, char **argument_values)
{
	g_setenv ("TZ", "Europe/Zurich", TRUE);
	tzset ();
	gtk_init (&argument_count, &argument_values);
	g_test_init (&argument_count, &argument_values, NULL);
	if (g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR")) {
		bindtextdomain (GETTEXT_PACKAGE, g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR"));
		bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
		textdomain (GETTEXT_PACKAGE);
		if (g_strcmp0 (g_getenv ("LANGUAGE"), "de") == 0) {
			g_assert_cmpstr (_("Replied"), ==, "Beantwortet");
			g_assert_cmpstr (_("Replied to all"), ==, "Allen geantwortet");
			g_assert_cmpstr (_("Replied and forwarded"), ==, "Beantwortet und weitergeleitet");
			g_assert_cmpstr (_("You replied to this message."), ==, "Sie haben auf diese Nachricht geantwortet.");
			g_assert_cmpstr (_("You replied to all recipients of this message."), ==, "Sie haben allen Empfängern dieser Nachricht geantwortet.");
			g_assert_cmpstr (_("You forwarded this message."), ==, "Sie haben diese Nachricht weitergeleitet.");
		}
	}
	g_test_add_func ("/blueevolution/mail/date-width", test_widths);
	g_test_add_func ("/blueevolution/mail/mouse-wheel-scroll", test_mouse_wheel_scroll);
	g_test_add_func ("/blueevolution/mail/mouse-wheel-scroll-frames", test_mouse_wheel_scroll_frames);
	g_test_add_func ("/blueevolution/mail/date-headings-top-level", test_date_headings_only_for_top_level_rows);
	g_test_add_func ("/blueevolution/mail/older-date-weekday-abbreviated", test_older_dates_use_abbreviated_weekday);
	g_test_add_func ("/blueevolution/mail/thread-expand-default-closed", test_thread_expand_default_closed);
	g_test_add_func ("/blueevolution/mail/collapsed-thread-latest-message", test_collapsed_thread_latest_message);
	g_test_add_func ("/blueevolution/mail/unread-filter-refresh", test_unread_filter_refresh);
	g_test_add_func ("/blueevolution/mail/note-replacement", test_note_replacement);
	g_test_add_func ("/blueevolution/mail/unread-conversation-search", test_unread_conversation_search);
	g_test_add_func ("/blueevolution/mail/conversation-adapter-order", test_conversation_adapter_order);
	g_test_add_func ("/blueevolution/mail/reply-preserves-unread-default", test_reply_preserves_unread_default);
	g_test_add_func ("/blueevolution/mail/status-icons", test_message_status_icons);
	g_test_add_func ("/blueevolution/mail/status-header", test_message_status_header);
	g_test_add_func ("/blueevolution/mail/date-sections", test_dates);
	g_test_add_func ("/blueevolution/mail/unread-row-background", test_unread_row_background);
	g_test_add_func ("/blueevolution/mail/expander-under-initials", test_expander_placement);
	g_test_add_func ("/blueevolution/mail/attachment-symbolic-foreground", test_attachment_symbolic_foreground);
	g_test_add_func ("/blueevolution/mail/attachment-stream-foreground", test_attachment_stream_foreground);
	g_test_add_func ("/blueevolution/mail/attachment-stream-preserves-image", test_attachment_stream_preserves_image);
	return g_test_run ();
}