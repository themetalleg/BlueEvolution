#include "e-util/e-util.h"

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
test_tracking_settings_controls (void)
{
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *widget = tracking_page_new (NULL);
	TrackingPage *page = tracking_page_from_widget (widget);
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
	g_test_add_func ("/wordpress-tracking/mime/strip-inherited-pixel", test_tracking_ids_and_pixel_cleanup);
	g_test_add_func ("/wordpress-tracking/mime/reject-signed-message", test_tracking_rejects_signed_messages);
	return g_test_run ();
}