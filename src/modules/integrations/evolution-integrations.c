#include "evolution-config.h"
#include <glib/gi18n-lib.h>
#include "e-util/e-util.h"
#include "shell/e-shell.h"

typedef struct { EExtension parent; } EIntegrations;
typedef struct { EExtensionClass parent; } EIntegrationsClass;
GType e_integrations_get_type (void);
void e_module_load (GTypeModule *module);
void e_module_unload (GTypeModule *module);
G_DEFINE_DYNAMIC_TYPE (EIntegrations, e_integrations, E_TYPE_EXTENSION)

typedef struct {
	EOpenAIManager *manager;
	GCancellable *cancellable;
	GtkWidget *provider, *model, *model_save, *account;
	GtkWidget *entry, *save, *test, *remove, *status, *feedback, *spinner;
	gboolean closed, configured, busy;
} IntegrationPage;

static void
page_free (gpointer data)
{
	IntegrationPage *page = data;
	g_clear_object (&page->manager);
	g_clear_object (&page->cancellable);
	g_free (page);
}

static void
page_destroyed (GtkWidget *widget, IntegrationPage *page)
{
	page->closed = TRUE;
	g_cancellable_cancel (page->cancellable);
}

static void
page_busy (IntegrationPage *page, gboolean busy)
{
	gchar *model = e_openai_manager_dup_model (page->manager);
	page->busy = busy;
	gtk_widget_set_sensitive (page->provider, !busy);
	gtk_widget_set_sensitive (page->model, !busy);
	gtk_widget_set_sensitive (page->model_save, !busy);
	gtk_widget_set_sensitive (page->entry, !busy);
	gtk_widget_set_sensitive (page->save, !busy);
	gtk_widget_set_sensitive (page->test, !busy && page->configured && !*gtk_entry_get_text (GTK_ENTRY (page->entry)) &&
		g_strcmp0 (model, gtk_entry_get_text (GTK_ENTRY (page->model))) == 0);
	g_free (model);
	gtk_widget_set_sensitive (page->remove, !busy && page->configured);
	if (busy)
		gtk_spinner_start (GTK_SPINNER (page->spinner));
	else
		gtk_spinner_stop (GTK_SPINNER (page->spinner));
}

static void
key_status_done (GObject *source, GAsyncResult *result, gpointer data)
{
	GtkWidget *widget = data;
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	GError *error = NULL;
	gchar *text = e_openai_manager_request_finish (E_OPENAI_MANAGER (source), result, &error);
	if (!page->closed) {
		page->configured = text && *text;
		gtk_entry_set_placeholder_text (GTK_ENTRY (page->entry), page->configured ?
			_("API key saved (hidden). Enter a new key to replace it.") : _("API key"));
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (page->entry), GTK_ENTRY_ICON_SECONDARY,
			page->configured ? "changes-prevent-symbolic" : NULL);
		gtk_entry_set_icon_tooltip_text (GTK_ENTRY (page->entry), GTK_ENTRY_ICON_SECONDARY,
			page->configured ? _("API key securely stored; its value is never displayed.") : NULL);
		gtk_button_set_label (GTK_BUTTON (page->save), page->configured ? _("_Replace API Key") : _("_Save API Key"));
		gtk_label_set_text (GTK_LABEL (page->status), error ? error->message : page->configured ?
			_("API key stored in the system keyring.") : _("No API key configured."));
		page_busy (page, FALSE);
	}
	g_clear_error (&error);
	g_free (text);
	g_object_unref (widget);
}

static void
key_test_done (GObject *source, GAsyncResult *result, gpointer data)
{
	GtkWidget *widget = data;
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	GError *error = NULL;
	gchar *text = e_openai_manager_request_finish (E_OPENAI_MANAGER (source), result, &error);
	if (!page->closed) {
		gtk_image_set_from_icon_name (GTK_IMAGE (page->feedback), text ? "emblem-ok-symbolic" : "dialog-error-symbolic", GTK_ICON_SIZE_BUTTON);
		gtk_widget_set_tooltip_text (page->feedback, text ? _("Reply generation test succeeded") : _("API key test failed"));
		gtk_label_set_text (GTK_LABEL (page->status), error ? error->message : _("Reply generation works now. Quota and rate limits can change."));
		page_busy (page, FALSE);
	}
	g_clear_error (&error);
	g_free (text);
	g_object_unref (widget);
}

static void
key_saved (GObject *source, GAsyncResult *result, gpointer data)
{
	GtkWidget *widget = data;
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	GError *error = NULL;
	gboolean saved = e_openai_manager_set_key_finish (E_OPENAI_MANAGER (source), result, &error);
	if (!page->closed) {
		gtk_entry_set_text (GTK_ENTRY (page->entry), "");
		gtk_image_clear (GTK_IMAGE (page->feedback));
		if (!saved) {
			gtk_label_set_text (GTK_LABEL (page->status), error ? error->message : _("The API key could not be saved securely."));
			page_busy (page, FALSE);
		} else {
			e_openai_manager_request (page->manager, E_OPENAI_HAS_KEY, NULL, NULL, NULL,
				page->cancellable, key_status_done, g_object_ref (widget));
		}
	}
	g_clear_error (&error);
	g_object_unref (widget);
}

static void
save_clicked (GtkButton *button, GtkWidget *widget)
{
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	const gchar *key = gtk_entry_get_text (GTK_ENTRY (page->entry));
	GtkWidget *dialog;
	gchar *title, *notice;
	gint response;
	if (!*key)
		return;
	title = g_strdup_printf (_("Enable %s email assistance?"), e_openai_manager_get_provider_name (page->manager));
	notice = g_strdup_printf (_("Opening the assistant sends the current email draft, quoted message, subject, and your instructions to %s. "
		"This can include personal or confidential information. Attachments are not uploaded. "
		"The provider's data policies, retention rules, and API charges apply. Review them before continuing. "
		"Generated drafts must be reviewed before sending. Your API key will be stored in your desktop's secure keyring, not in application settings."),
		e_openai_manager_get_provider_name (page->manager));
	dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (widget)),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
		"%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", notice);
	g_free (title);
	g_free (notice);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL,
		_("I Understand, Save Key"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
	g_object_ref (widget);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	if (response == GTK_RESPONSE_ACCEPT && !page->closed) {
		page_busy (page, TRUE);
		e_openai_manager_set_key (page->manager, key, TRUE, page->cancellable, key_saved, g_object_ref (widget));
	}
	g_object_unref (widget);
}

static void
key_edited (GtkEntry *entry, IntegrationPage *page)
{
	gtk_image_clear (GTK_IMAGE (page->feedback));
	page_busy (page, page->busy);
}

static void
test_clicked (GtkButton *button, GtkWidget *widget)
{
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	page_busy (page, TRUE);
	gtk_image_clear (GTK_IMAGE (page->feedback));
	gtk_label_set_text (GTK_LABEL (page->status), _("Testing reply generation..."));
	e_openai_manager_request (page->manager, E_OPENAI_TEST_KEY, NULL, NULL, NULL,
		page->cancellable, key_test_done, g_object_ref (widget));
}

static void
remove_clicked (GtkButton *button, GtkWidget *widget)
{
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	page_busy (page, TRUE);
	e_openai_manager_set_key (page->manager, NULL, FALSE, page->cancellable, key_saved, g_object_ref (widget));
}

static void
model_saved (GtkWidget *button, IntegrationPage *page)
{
	gboolean saved = e_openai_manager_set_model (page->manager, gtk_entry_get_text (GTK_ENTRY (page->model)));
	gtk_image_clear (GTK_IMAGE (page->feedback));
	gtk_label_set_text (GTK_LABEL (page->status), saved ? _("Model saved. Test API access before generating replies.") :
		_("Enter a valid model ID using letters, numbers, dots, hyphens, or underscores."));
	page_busy (page, page->busy);
}

static void
model_edited (GtkEntry *entry, IntegrationPage *page)
{
	gchar *saved = e_openai_manager_dup_model (page->manager);
	gtk_image_clear (GTK_IMAGE (page->feedback));
	page_busy (page, page->busy);
	gtk_widget_set_sensitive (page->test, !page->busy && page->configured &&
		!*gtk_entry_get_text (GTK_ENTRY (page->entry)) && g_strcmp0 (saved, gtk_entry_get_text (entry)) == 0);
	g_free (saved);
}

static void
provider_changed (GtkComboBox *combo, GtkWidget *widget)
{
	IntegrationPage *page = g_object_get_data (G_OBJECT (widget), "integration-page");
	const gchar *provider = gtk_combo_box_get_active_id (combo);
	const gchar *uri, *account;
	GSettings *settings;
	gchar *model;
	if (!provider || page->busy)
		return;
	g_clear_object (&page->manager);
	page->manager = e_openai_manager_new_for_provider (provider);
	settings = g_settings_new ("io.github.themetalleg.BlueEvolution");
	g_settings_set_string (settings, "mail-assistant-provider", provider);
	g_object_unref (settings);
	page->configured = FALSE;
	gtk_entry_set_text (GTK_ENTRY (page->entry), "");
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (page->entry), GTK_ENTRY_ICON_SECONDARY, NULL);
	gtk_entry_set_placeholder_text (GTK_ENTRY (page->entry), _("API key"));
	atk_object_set_name (gtk_widget_get_accessible (page->entry), e_openai_manager_get_provider_name (page->manager));
	model = e_openai_manager_dup_model (page->manager);
	gtk_entry_set_text (GTK_ENTRY (page->model), model);
	g_free (model);
	if (g_str_equal (provider, "gemini")) {
		uri = "https://aistudio.google.com/apikey";
		account = _("Google AI Studio API keys");
	} else if (g_str_equal (provider, "claude")) {
		uri = "https://platform.claude.com/settings/keys";
		account = _("Anthropic API keys");
	} else {
		uri = "https://platform.openai.com/api-keys";
		account = _("OpenAI API keys");
	}
	gtk_link_button_set_uri (GTK_LINK_BUTTON (page->account), uri);
	gtk_button_set_label (GTK_BUTTON (page->account), account);
	gtk_label_set_text (GTK_LABEL (page->status), _("Checking secure key storage..."));
	page_busy (page, TRUE);
	e_openai_manager_request (page->manager, E_OPENAI_HAS_KEY, NULL, NULL, NULL,
		page->cancellable, key_status_done, g_object_ref (widget));
}

static GtkWidget *
integrations_page_new (EPreferencesWindow *window)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *label = gtk_label_new (NULL);
	IntegrationPage *page = g_new0 (IntegrationPage, 1);
	page->manager = e_openai_manager_ref_default ();
	page->cancellable = g_cancellable_new ();
	g_object_set_data_full (G_OBJECT (box), "integration-page", page, page_free);
	g_signal_connect (box, "destroy", G_CALLBACK (page_destroyed), page);
	gtk_container_set_border_width (GTK_CONTAINER (box), 18);
	gtk_label_set_text (GTK_LABEL (label), _("Mail Assistant"));
	gtk_style_context_add_class (gtk_widget_get_style_context (label), "heading");
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	page->provider = gtk_combo_box_text_new ();
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (page->provider), "openai", "OpenAI");
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (page->provider), "gemini", "Google Gemini");
	gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (page->provider), "claude", "Claude (Anthropic)");
	label = gtk_label_new_with_mnemonic (_("_Provider:"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), page->provider);
	gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->provider, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	page->model = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY (page->model), 128);
	label = gtk_label_new_with_mnemonic (_("_Model:"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), page->model);
	gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->model, TRUE, TRUE, 0);
	page->model_save = gtk_button_new_from_icon_name ("document-save-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text (page->model_save, _("Save model"));
	atk_object_set_name (gtk_widget_get_accessible (page->model_save), _("Save model"));
	gtk_box_pack_start (GTK_BOX (row), page->model_save, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
	page->account = gtk_link_button_new ("https://platform.openai.com/api-keys");
	gtk_widget_set_halign (page->account, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), page->account, FALSE, FALSE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	page->entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (page->entry), FALSE);
	gtk_entry_set_input_purpose (GTK_ENTRY (page->entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_max_length (GTK_ENTRY (page->entry), 512);
	gtk_entry_set_placeholder_text (GTK_ENTRY (page->entry), _("API key"));
	atk_object_set_name (gtk_widget_get_accessible (page->entry), _("API key"));
	gtk_widget_set_hexpand (page->entry, TRUE);
	gtk_box_pack_start (GTK_BOX (row), page->entry, TRUE, TRUE, 0);
	page->save = gtk_button_new_with_mnemonic (_("_Save API Key"));
	gtk_button_set_image (GTK_BUTTON (page->save), gtk_image_new_from_icon_name ("document-save-symbolic", GTK_ICON_SIZE_BUTTON));
	gtk_box_pack_start (GTK_BOX (row), page->save, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	page->test = gtk_button_new_with_label (_("Test API Key"));
	gtk_widget_set_tooltip_text (page->test, _("Tests reply generation with a short synthetic prompt, not email content. A small API charge may apply."));
	gtk_button_set_image (GTK_BUTTON (page->test), gtk_image_new_from_icon_name ("system-run-symbolic", GTK_ICON_SIZE_BUTTON));
	page->remove = gtk_button_new_with_label (_("Remove API Key"));
	gtk_button_set_image (GTK_BUTTON (page->remove), gtk_image_new_from_icon_name ("edit-delete-symbolic", GTK_ICON_SIZE_BUTTON));
	page->feedback = gtk_image_new ();
	page->spinner = gtk_spinner_new ();
	gtk_box_pack_start (GTK_BOX (row), page->test, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->remove, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->spinner, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->feedback, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
	page->status = gtk_label_new (_("Checking secure key storage..."));
	gtk_label_set_selectable (GTK_LABEL (page->status), TRUE);
	gtk_label_set_line_wrap (GTK_LABEL (page->status), TRUE);
	gtk_label_set_xalign (GTK_LABEL (page->status), 0);
	gtk_box_pack_start (GTK_BOX (box), page->status, FALSE, FALSE, 0);
	g_signal_connect (page->save, "clicked", G_CALLBACK (save_clicked), box);
	g_signal_connect (page->test, "clicked", G_CALLBACK (test_clicked), box);
	g_signal_connect (page->remove, "clicked", G_CALLBACK (remove_clicked), box);
	g_signal_connect (page->entry, "changed", G_CALLBACK (key_edited), page);
	g_signal_connect (page->provider, "changed", G_CALLBACK (provider_changed), box);
	g_signal_connect (page->model_save, "clicked", G_CALLBACK (model_saved), page);
	g_signal_connect (page->model, "activate", G_CALLBACK (model_saved), page);
	g_signal_connect (page->model, "changed", G_CALLBACK (model_edited), page);
	gtk_widget_show_all (box);
	{
		gchar *provider = g_strdup (e_openai_manager_get_provider (page->manager));
		gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->provider), provider);
		g_free (provider);
	}
	return box;
}

static void
integrations_constructed (GObject *object)
{
	EShell *shell = E_SHELL (e_extension_get_extensible (E_EXTENSION (object)));
	e_preferences_window_add_page (E_PREFERENCES_WINDOW (e_shell_get_preferences_window (shell)),
		"page-integrations", "preferences-system", _("Integrations"), NULL, integrations_page_new, 960);
	G_OBJECT_CLASS (e_integrations_parent_class)->constructed (object);
}
static void e_integrations_init (EIntegrations *self) {}
static void e_integrations_class_finalize (EIntegrationsClass *class) {}
static void e_integrations_class_init (EIntegrationsClass *class)
{
	G_OBJECT_CLASS (class)->constructed = integrations_constructed;
	E_EXTENSION_CLASS (class)->extensible_type = E_TYPE_SHELL;
}
G_MODULE_EXPORT void e_module_load (GTypeModule *module) { e_integrations_register_type (module); }
G_MODULE_EXPORT void e_module_unload (GTypeModule *module) {}