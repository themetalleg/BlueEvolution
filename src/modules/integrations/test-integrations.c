#include "e-util/e-util.h"

static guint saved_keys;

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
	return g_test_run ();
}