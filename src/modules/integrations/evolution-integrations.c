#include "evolution-config.h"
#include <glib/gi18n-lib.h>
#include "e-util/e-util.h"
#include "shell/e-shell.h"
#include "mail/e-mail-reader.h"

typedef struct { EExtension parent; } EIntegrations;
typedef struct { EExtensionClass parent; } EIntegrationsClass;
GType e_integrations_get_type (void);
void e_module_load (GTypeModule *module);
void e_module_unload (GTypeModule *module);
G_DEFINE_DYNAMIC_TYPE (EIntegrations, e_integrations, E_TYPE_EXTENSION)

typedef struct {
	EExtension parent;
	EWordPressTrackingManager *manager;
	EAlert *alert;
	gchar *current_message_uid;
} ETrackingReader;
typedef struct { EExtensionClass parent; } ETrackingReaderClass;
GType e_tracking_reader_get_type (void);
G_DEFINE_DYNAMIC_TYPE (ETrackingReader, e_tracking_reader, E_TYPE_EXTENSION)

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

typedef struct {
	EWordPressTrackingManager *manager;
	GtkWidget *site_url, *project_id, *api_key, *enabled, *default_enabled;
	GtkWidget *clear_key, *save, *test, *remove, *status, *feedback, *spinner;
	gboolean busy, key_saved;
} TrackingPage;

static TrackingPage *
tracking_page_from_widget (GtkWidget *widget)
{
	return g_object_get_data (G_OBJECT (widget), "tracking-page");
}

static void
tracking_page_free (gpointer data)
{
	TrackingPage *page = data;
	g_clear_object (&page->manager);
	g_free (page);
}

static void
tracking_page_busy (TrackingPage *page,
		    gboolean busy)
{
	page->busy = busy;
	gtk_widget_set_sensitive (page->site_url, !busy);
	gtk_widget_set_sensitive (page->project_id, !busy);
	gtk_widget_set_sensitive (page->api_key, !busy);
	gtk_widget_set_sensitive (page->enabled, !busy);
	gtk_widget_set_sensitive (page->default_enabled, !busy &&
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->enabled)));
	gtk_widget_set_sensitive (page->clear_key, !busy && page->key_saved);
	gtk_widget_set_sensitive (page->save, !busy);
	gtk_widget_set_sensitive (page->test, !busy);
	gtk_widget_set_sensitive (page->remove, !busy);
	if (busy)
		gtk_spinner_start (GTK_SPINNER (page->spinner));
	else
		gtk_spinner_stop (GTK_SPINNER (page->spinner));
}

static void
tracking_save_clicked (GtkButton *button,
		       GtkWidget *widget)
{
	TrackingPage *page = tracking_page_from_widget (widget);
	const gchar *site_url = gtk_entry_get_text (GTK_ENTRY (page->site_url));
	const gchar *project_id = gtk_entry_get_text (GTK_ENTRY (page->project_id));
	const gchar *api_key = gtk_entry_get_text (GTK_ENTRY (page->api_key));
	gboolean enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->enabled));
	gboolean track_by_default = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->default_enabled));
	gboolean clear_key = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->clear_key));
	gboolean has_key;
	GError *error = NULL;

	gtk_image_clear (GTK_IMAGE (page->feedback));
	if ((enabled || *site_url || *project_id) &&
	    !e_wordpress_tracking_manager_validate_configuration (site_url, project_id, &error)) {
		gtk_label_set_text (GTK_LABEL (page->status), error->message);
		g_clear_error (&error);
		return;
	}
	has_key = e_wordpress_tracking_manager_has_key (page->manager, &error);
	if (error) {
		gtk_label_set_text (GTK_LABEL (page->status), error->message);
		g_clear_error (&error);
		return;
	}
	if (enabled && !*api_key && (clear_key || !has_key)) {
		gtk_label_set_text (GTK_LABEL (page->status),
			_("Store a WordPress project API key before enabling tracking."));
		return;
	}
	if (*api_key && !e_wordpress_tracking_manager_store_key (page->manager, api_key, &error)) {
		gtk_label_set_text (GTK_LABEL (page->status), error ? error->message :
			_("The WordPress API key could not be stored securely."));
		g_clear_error (&error);
		return;
	}
	if (clear_key && !*api_key && has_key &&
	    !e_wordpress_tracking_manager_remove_key (page->manager, &error)) {
		gtk_label_set_text (GTK_LABEL (page->status), error ? error->message :
			_("The saved WordPress API key could not be removed."));
		g_clear_error (&error);
		return;
	}
	page->key_saved = (*api_key != '\0') || (has_key && !clear_key);
	e_wordpress_tracking_manager_set_site_url (page->manager, site_url);
	e_wordpress_tracking_manager_set_project_id (page->manager, project_id);
	e_wordpress_tracking_manager_set_enabled (page->manager, enabled);
	e_wordpress_tracking_manager_set_default (page->manager, track_by_default);
	gtk_entry_set_text (GTK_ENTRY (page->api_key), "");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->clear_key), FALSE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (page->api_key), *api_key || (has_key && !clear_key) ?
		_("API key saved (hidden). Enter a new key to replace it.") : _("Project API key"));
	gtk_label_set_text (GTK_LABEL (page->status), enabled ?
		_("WordPress email tracking settings saved.") : _("WordPress email tracking is disabled."));
	gtk_image_set_from_icon_name (GTK_IMAGE (page->feedback), "emblem-ok-symbolic", GTK_ICON_SIZE_BUTTON);
}

static void
tracking_test_done (GObject *source,
		    GAsyncResult *result,
		    gpointer user_data)
{
	GtkWidget *widget = user_data;
	TrackingPage *page = tracking_page_from_widget (widget);
	GError *error = NULL;
	gboolean success = e_wordpress_tracking_manager_test_finish (
		E_WORDPRESS_TRACKING_MANAGER (source), result, &error);
	if (page) {
		gtk_image_set_from_icon_name (GTK_IMAGE (page->feedback),
			success ? "emblem-ok-symbolic" : "dialog-error-symbolic", GTK_ICON_SIZE_BUTTON);
		gtk_label_set_text (GTK_LABEL (page->status), error ? error->message :
			_("WordPress connection and token resolver are available."));
		tracking_page_busy (page, FALSE);
	}
	g_clear_error (&error);
	g_object_unref (widget);
}

static void
tracking_test_clicked (GtkButton *button,
		       GtkWidget *widget)
{
	TrackingPage *page = tracking_page_from_widget (widget);
	tracking_page_busy (page, TRUE);
	gtk_image_clear (GTK_IMAGE (page->feedback));
	gtk_label_set_text (GTK_LABEL (page->status), _("Testing WordPress connection and resolver…"));
	e_wordpress_tracking_manager_test_async (page->manager, NULL, tracking_test_done, g_object_ref (widget));
}

static void
tracking_remove_clicked (GtkButton *button,
			 GtkWidget *widget)
{
	TrackingPage *page = tracking_page_from_widget (widget);
	GError *error = NULL;
	if (!e_wordpress_tracking_manager_remove_key (page->manager, &error)) {
		gtk_label_set_text (GTK_LABEL (page->status), error ? error->message :
			_("The saved WordPress API key could not be removed."));
		g_clear_error (&error);
		return;
	}
	e_wordpress_tracking_manager_set_enabled (page->manager, FALSE);
	e_wordpress_tracking_manager_set_default (page->manager, FALSE);
	page->key_saved = FALSE;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->enabled), FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->default_enabled), FALSE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (page->api_key), _("Project API key"));
	gtk_image_clear (GTK_IMAGE (page->feedback));
	gtk_label_set_text (GTK_LABEL (page->status), _("Saved WordPress API key removed; tracking disabled."));
}

static GtkWidget *
tracking_page_new (EPreferencesWindow *window)
{
	const gchar *site_url, *project_id;
	GError *error = NULL;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *grid = gtk_grid_new ();
	GtkWidget *label, *row;
	TrackingPage *page = g_new0 (TrackingPage, 1);
	gboolean has_key;
	page->manager = e_wordpress_tracking_manager_new ();
	g_object_set_data_full (G_OBJECT (box), "tracking-page", page, tracking_page_free);
	gtk_container_set_border_width (GTK_CONTAINER (box), 18);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 12);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
	label = gtk_label_new (NULL);
	gtk_label_set_text (GTK_LABEL (label), _("WordPress Email Tracking"));
	gtk_style_context_add_class (gtk_widget_get_style_context (label), "heading");
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	label = gtk_label_new (_("Register a unique tracking pixel for each outgoing email. Image requests do not prove that a person read the message."));
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	label = gtk_link_button_new_with_label ("https://github.com/themetalleg/wordpress-tracking-pixel/releases/",
		_("WordPress Tracking Pixel releases"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	label = gtk_label_new_with_mnemonic (_("_WordPress home URL (HTTPS):"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	page->site_url = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (page->site_url), "https://example.com/wordpress");
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), page->site_url);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), page->site_url, 1, 0, 1, 1);
	label = gtk_label_new_with_mnemonic (_("Project _ID:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	page->project_id = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY (page->project_id), 10);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), page->project_id);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), page->project_id, 1, 1, 1, 1);
	label = gtk_label_new_with_mnemonic (_("Project _API key:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	page->api_key = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (page->api_key), FALSE);
	gtk_entry_set_input_purpose (GTK_ENTRY (page->api_key), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_max_length (GTK_ENTRY (page->api_key), 90);
	gtk_entry_set_placeholder_text (GTK_ENTRY (page->api_key), _("Project API key"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), page->api_key);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), page->api_key, 1, 2, 1, 1);
	gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);
	page->enabled = gtk_check_button_new_with_mnemonic (_("_Enable WordPress tracking"));
	page->default_enabled = gtk_check_button_new_with_mnemonic (_("Track email openings _by default"));
	gtk_box_pack_start (GTK_BOX (box), page->enabled, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), page->default_enabled, FALSE, FALSE, 0);
	page->clear_key = gtk_check_button_new_with_mnemonic (_("_Remove saved API key"));
	gtk_box_pack_start (GTK_BOX (box), page->clear_key, FALSE, FALSE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	page->save = gtk_button_new_with_mnemonic (_("_Save Settings"));
	gtk_button_set_image (GTK_BUTTON (page->save), gtk_image_new_from_icon_name ("document-save-symbolic", GTK_ICON_SIZE_BUTTON));
	page->test = gtk_button_new_with_mnemonic (_("_Test Connection"));
	gtk_button_set_image (GTK_BUTTON (page->test), gtk_image_new_from_icon_name ("system-run-symbolic", GTK_ICON_SIZE_BUTTON));
	page->remove = gtk_button_new_with_mnemonic (_("Remove _Key"));
	gtk_button_set_image (GTK_BUTTON (page->remove), gtk_image_new_from_icon_name ("edit-delete-symbolic", GTK_ICON_SIZE_BUTTON));
	page->spinner = gtk_spinner_new ();
	page->feedback = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (row), page->save, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->test, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->remove, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->spinner, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), page->feedback, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
	page->status = gtk_label_new ("");
	gtk_label_set_selectable (GTK_LABEL (page->status), TRUE);
	gtk_label_set_line_wrap (GTK_LABEL (page->status), TRUE);
	gtk_label_set_xalign (GTK_LABEL (page->status), 0);
	gtk_box_pack_start (GTK_BOX (box), page->status, FALSE, FALSE, 0);
	g_signal_connect (page->save, "clicked", G_CALLBACK (tracking_save_clicked), box);
	g_signal_connect (page->test, "clicked", G_CALLBACK (tracking_test_clicked), box);
	g_signal_connect (page->remove, "clicked", G_CALLBACK (tracking_remove_clicked), box);
	site_url = e_wordpress_tracking_manager_dup_site_url (page->manager);
	project_id = e_wordpress_tracking_manager_dup_project_id (page->manager);
	gtk_entry_set_text (GTK_ENTRY (page->site_url), site_url);
	gtk_entry_set_text (GTK_ENTRY (page->project_id), project_id);
	g_free ((gchar *) site_url);
	g_free ((gchar *) project_id);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->enabled),
		e_wordpress_tracking_manager_get_enabled (page->manager));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->default_enabled),
		e_wordpress_tracking_manager_get_default (page->manager));
	has_key = e_wordpress_tracking_manager_has_key (page->manager, &error);
	if (has_key) {
		page->key_saved = TRUE;
		gtk_entry_set_placeholder_text (GTK_ENTRY (page->api_key), _("API key saved (hidden). Enter a new key to replace it."));
		gtk_widget_set_sensitive (page->remove, TRUE);
	} else {
		gtk_widget_set_sensitive (page->remove, FALSE);
		gtk_label_set_text (GTK_LABEL (page->status), error ? error->message : _("No WordPress project API key configured."));
	}
	g_clear_error (&error);
	gtk_widget_set_sensitive (page->default_enabled, e_wordpress_tracking_manager_get_enabled (page->manager));
	gtk_widget_set_sensitive (page->test, has_key);
	gtk_widget_show_all (box);
	return box;
}

typedef struct {
	ETrackingReader *extension;
	gchar *message_uid;
} TrackingSummaryRequest;

static void
tracking_summary_request_free (gpointer data)
{
	TrackingSummaryRequest *request = data;
	g_object_unref (request->extension);
	g_free (request->message_uid);
	g_free (request);
}

static void
tracking_reader_clear_alert (ETrackingReader *self)
{
	if (self->alert) {
		e_alert_response (self->alert, GTK_RESPONSE_CLOSE);
		g_clear_object (&self->alert);
	}
}

static gchar *
tracking_format_time (const gchar *value)
{
	GDateTime *parsed, *local;
	gchar *formatted;
	if (!value || !*value)
		return g_strdup (_("No request recorded"));
	parsed = g_date_time_new_from_iso8601 (value, NULL);
	if (!parsed)
		return g_strdup (value);
	local = g_date_time_to_local (parsed);
	formatted = g_date_time_format (local, "%x %X %Z");
	g_date_time_unref (local);
	g_date_time_unref (parsed);
	return formatted;
}

static void
tracking_reader_show_alert (ETrackingReader *self,
			    EMailReader *reader,
			    const gchar *tag,
			    const gchar *arg0,
			    const gchar *arg1,
			    const gchar *arg2)
{
	EPreviewPane *preview = e_mail_reader_get_preview_pane (reader);
	EAlert *alert;
	if (!preview)
		return;
	alert = e_alert_new (tag, arg0 ? arg0 : "", arg1 ? arg1 : "", arg2 ? arg2 : "", NULL);
	if (!alert)
		return;
	tracking_reader_clear_alert (self);
	e_alert_sink_submit_alert (E_ALERT_SINK (preview), alert);
	self->alert = g_object_ref (alert);
	g_object_unref (alert);
}

static void
tracking_summary_done (GObject *source_object,
		       GAsyncResult *result,
		       gpointer user_data)
{
	TrackingSummaryRequest *request = user_data;
	ETrackingReader *self = request->extension;
	EMailReader *reader = E_MAIL_READER (e_extension_get_extensible (E_EXTENSION (self)));
	EWordPressTrackingSummary *summary;
	GError *error = NULL;
	const gchar *current_uid = self->current_message_uid;
	if (!current_uid || g_strcmp0 (current_uid, request->message_uid) != 0)
		goto exit;
	summary = e_wordpress_tracking_manager_summary_finish (
		E_WORDPRESS_TRACKING_MANAGER (source_object), result, &error);
	if (!summary) {
		if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			tracking_reader_show_alert (self, reader, "mail:wordpress-tracking-error", error->message, NULL, NULL);
		goto exit;
	}
	{
		gchar *count = g_strdup_printf ("%" G_GUINT64_FORMAT,
			e_wordpress_tracking_summary_get_count (summary));
		gchar *first = tracking_format_time (e_wordpress_tracking_summary_get_first_request (summary));
		gchar *latest = tracking_format_time (e_wordpress_tracking_summary_get_latest_request (summary));
		tracking_reader_show_alert (self, reader, "mail:wordpress-tracking-summary", count, first, latest);
		g_free (count);
		g_free (first);
		g_free (latest);
	}
	e_wordpress_tracking_summary_free (summary);
exit:
	g_clear_error (&error);
	tracking_summary_request_free (request);
}

static gboolean
tracking_valid_external_id (const gchar *value)
{
	gsize ii;
	if (!value || strlen (value) != 128)
		return FALSE;
	for (ii = 0; ii < 128; ii++) {
		if (!g_ascii_isalnum (value[ii]))
			return FALSE;
	}
	return TRUE;
}

static gboolean
tracking_parse_header (const gchar *header,
		       gchar **out_project,
		       gchar **out_external)
{
	gchar **parts;
	gchar *version = NULL, *project = NULL, *external = NULL;
	guint ii;
	gboolean valid, conflicting = FALSE;
	if (!header || !*header)
		return FALSE;
	parts = g_strsplit (header, ";", -1);
	for (ii = 0; parts[ii]; ii++) {
		gchar *part = g_strstrip (parts[ii]);
		if (g_str_has_prefix (part, "v=")) {
			const gchar *value = part + 2;
			if (version && g_strcmp0 (version, value) != 0)
				conflicting = TRUE;
			else if (!version)
				version = g_strdup (value);
		} else if (g_str_has_prefix (part, "project=")) {
			const gchar *value = part + strlen ("project=");
			if (project && g_strcmp0 (project, value) != 0)
				conflicting = TRUE;
			else if (!project)
				project = g_strdup (value);
		} else if (g_str_has_prefix (part, "external=")) {
			const gchar *value = part + strlen ("external=");
			if (external && g_strcmp0 (external, value) != 0)
				conflicting = TRUE;
			else if (!external)
				external = g_strdup (value);
		}
	}
	valid = !conflicting && g_strcmp0 (version, "1") == 0 && project && tracking_valid_external_id (external);
	if (valid) {
		*out_project = g_steal_pointer (&project);
		*out_external = g_steal_pointer (&external);
	}
	g_free (version);
	g_free (project);
	g_free (external);
	g_strfreev (parts);
	return valid;
}

static void
tracking_reader_message_loaded_cb (EMailReader *reader,
				   const gchar *message_uid,
				   CamelMimeMessage *message,
				   ETrackingReader *self)
{
	const gchar *header = camel_medium_get_header (CAMEL_MEDIUM (message), "X-MiPipe-Tracking");
	gchar *project = NULL, *external = NULL, *configured_project = NULL;
	TrackingSummaryRequest *request;
	tracking_reader_clear_alert (self);
	g_free (self->current_message_uid);
	self->current_message_uid = g_strdup (message_uid);
	if (!tracking_parse_header (header, &project, &external))
		goto exit;
	configured_project = e_wordpress_tracking_manager_dup_project_id (self->manager);
	if (g_strcmp0 (project, configured_project) != 0)
		goto exit;
	request = g_new0 (TrackingSummaryRequest, 1);
	request->extension = g_object_ref (self);
	request->message_uid = g_strdup (message_uid);
	e_wordpress_tracking_manager_summary_async (self->manager, external, NULL, tracking_summary_done, request);
exit:
	g_free (project);
	g_free (external);
	g_free (configured_project);
}

static void
tracking_reader_constructed (GObject *object)
{
	ETrackingReader *self = (ETrackingReader *) object;
	EExtension *extension = E_EXTENSION (object);
	EExtensible *extensible = e_extension_get_extensible (extension);
	g_return_if_fail (E_IS_MAIL_READER (extensible));
	self->manager = e_wordpress_tracking_manager_new ();
	g_signal_connect_object (extensible, "message-loaded",
		G_CALLBACK (tracking_reader_message_loaded_cb), self, 0);
	G_OBJECT_CLASS (e_tracking_reader_parent_class)->constructed (object);
}

static void
tracking_reader_dispose (GObject *object)
{
	ETrackingReader *self = (ETrackingReader *) object;
	tracking_reader_clear_alert (self);
	g_clear_object (&self->manager);
	g_clear_pointer (&self->current_message_uid, g_free);
	G_OBJECT_CLASS (e_tracking_reader_parent_class)->dispose (object);
}

static void
tracking_reader_class_init (ETrackingReaderClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	EExtensionClass *extension_class = E_EXTENSION_CLASS (class);
	object_class->constructed = tracking_reader_constructed;
	object_class->dispose = tracking_reader_dispose;
	extension_class->extensible_type = E_TYPE_MAIL_READER;
}

static void
e_tracking_reader_class_finalize (ETrackingReaderClass *class)
{
}

static void
tracking_reader_init (ETrackingReader *self)
{
}

static void
integrations_constructed (GObject *object)
{
	EShell *shell = E_SHELL (e_extension_get_extensible (E_EXTENSION (object)));
	e_preferences_window_add_page (E_PREFERENCES_WINDOW (e_shell_get_preferences_window (shell)),
		"page-integrations", "preferences-system", _("Integrations"), NULL, integrations_page_new, 960);
	e_preferences_window_add_page (E_PREFERENCES_WINDOW (e_shell_get_preferences_window (shell)),
		"page-wordpress-tracking", "mail-mark-read", _("Email Tracking"), NULL, tracking_page_new, 970);
	G_OBJECT_CLASS (e_integrations_parent_class)->constructed (object);
}
static void e_integrations_init (EIntegrations *self) {}
static void e_integrations_class_finalize (EIntegrationsClass *class) {}
static void e_integrations_class_init (EIntegrationsClass *class)
{
	G_OBJECT_CLASS (class)->constructed = integrations_constructed;
	E_EXTENSION_CLASS (class)->extensible_type = E_TYPE_SHELL;
}
G_MODULE_EXPORT void e_module_load (GTypeModule *module)
{
	e_integrations_register_type (module);
	e_tracking_reader_register_type (module);
}
G_MODULE_EXPORT void e_module_unload (GTypeModule *module) {}