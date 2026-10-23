#include "e-composer-openai.c"

static void
test_suggestions (void)
{
	EComposerOpenAI *panel = g_object_new (e_composer_openai_get_type (), NULL);
	const gchar *valid = "{\"suggestions\":[{\"label\":\"Accept\",\"instruction\":\"Accept the proposal\"},{\"label\":\"Decline\",\"instruction\":\"Decline politely\"},{\"label\":\"Later\",\"instruction\":\"Ask to defer\"},{\"label\":\"Discuss\",\"instruction\":\"Ask for more details\"}]}";
	const gchar *invalid[] = { "[]", "{}", "invalid", "{\"suggestions\":{}}", "{\"suggestions\":[]}",
		"{\"suggestions\":[1,2,3,4]}", "{\"suggestions\":[{\"label\":true},{},{},{}]}" };
	GList *children;
	guint index;
	g_object_ref_sink (panel);
	panel->suggestions = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	panel->ideas = gtk_expander_new (_("Reply ideas"));
	gtk_container_add (GTK_CONTAINER (panel->ideas), panel->suggestions);
	gtk_box_pack_start (GTK_BOX (panel), panel->ideas, FALSE, FALSE, 0);
	gtk_expander_set_expanded (GTK_EXPANDER (panel->ideas), TRUE);
	g_assert_true (assistant_suggestions (panel, valid));
	for (index = 0; index < G_N_ELEMENTS (invalid); index++)
		g_assert_false (assistant_suggestions (panel, invalid[index]));
	children = gtk_container_get_children (GTK_CONTAINER (panel->suggestions));
	g_assert_cmpuint (g_list_length (children), ==, 4);
	g_assert_cmpstr (gtk_button_get_label (GTK_BUTTON (children->data)), ==, "Accept");
	g_assert_cmpstr (g_object_get_data (G_OBJECT (children->data), "reply-intention"), ==, "Accept the proposal");
	gtk_button_clicked (GTK_BUTTON (children->data));
	g_assert_false (gtk_expander_get_expanded (GTK_EXPANDER (panel->ideas)));
	g_list_free (children);
	gtk_widget_destroy (GTK_WIDGET (panel));
	g_object_unref (panel);
}

static void
test_conversation (void)
{
	EComposerOpenAI *panel = g_object_new (e_composer_openai_get_type (), NULL);
	GList *rows;
	GtkWidget *message;
	g_object_ref_sink (panel);
	panel->transcript = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_box_pack_start (GTK_BOX (panel), panel->transcript, TRUE, TRUE, 0);
	assistant_append (panel, TRUE, "Synthetic request");
	assistant_append (panel, FALSE, "Synthetic response");
	rows = gtk_container_get_children (GTK_CONTAINER (panel->transcript));
	g_assert_cmpuint (g_list_length (rows), ==, 2);
	g_assert_cmpint (gtk_widget_get_margin_start (rows->data), ==, 28);
	g_assert_cmpint (gtk_widget_get_margin_end (rows->next->data), ==, 28);
	{
		GList *messages = gtk_container_get_children (GTK_CONTAINER (rows->data));
		message = messages->data;
		g_assert_cmpint (gtk_widget_get_halign (message), ==, GTK_ALIGN_END);
		g_assert_true (gtk_style_context_has_class (gtk_widget_get_style_context (message), "mail-assistant-user"));
		g_list_free (messages);
		messages = gtk_container_get_children (GTK_CONTAINER (rows->next->data));
		message = messages->data;
		g_assert_cmpint (gtk_widget_get_halign (message), ==, GTK_ALIGN_START);
		g_assert_true (gtk_style_context_has_class (gtk_widget_get_style_context (message), "mail-assistant-reply"));
		g_list_free (messages);
	}
	g_list_free (rows);
	gtk_widget_destroy (GTK_WIDGET (panel));
	g_object_unref (panel);
}

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	if (g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR")) {
		const gchar *messages[] = { "Mail Assistant", "You", "Reply ideas", "Draft ready", "Inserted into draft",
			"Open or close the mail assistant", "Refresh reply ideas", "Integrations settings", "Close assistant",
			"Ask or refine the reply", "Message to assistant", "Generate reply", "Cancel request", "Insert at Cursor",
			"Add email content or enter a request.", "Contacting provider...", "Cancelled",
			"The provider returned invalid reply ideas. Try refreshing." };
		guint index;
		bindtextdomain (GETTEXT_PACKAGE, g_getenv ("BLUEEVOLUTION_TEST_LOCALEDIR"));
		bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
		if (g_strcmp0 (g_getenv ("LANGUAGE"), "de") == 0)
			for (index = 0; index < G_N_ELEMENTS (messages); index++)
				g_assert_cmpstr (_(messages[index]), !=, messages[index]);
	}
	g_test_add_func ("/openai/composer/suggestions", test_suggestions);
	g_test_add_func ("/assistant/composer/conversation", test_conversation);
	return g_test_run ();
}