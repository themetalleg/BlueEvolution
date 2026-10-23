#include "evolution-config.h"
#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>
#include "e-composer-openai.h"
#include "e-composer-header-table.h"
#include "shell/e-shell.h"

typedef struct {
	GtkBox parent;
	GWeakRef composer;
	EOpenAIManager *manager;
	GCancellable *cancellable;
	GtkWidget *suggestions, *transcript, *prompt, *send, *insert, *status, *spinner, *stop;
	GtkWidget *ideas, *chat_scroll;
	GtkWidget *revealer;
	EUIAction *toggle;
	gchar *draft;
	gchar *model;
	GString *history;
	guint serial;
	gboolean closed, busy;
} EComposerOpenAI;
typedef GtkBoxClass EComposerOpenAIClass;
static GType e_composer_openai_get_type (void);
G_DEFINE_TYPE (EComposerOpenAI, e_composer_openai, GTK_TYPE_BOX)

typedef struct {
	EComposerOpenAI *panel;
	guint serial;
	EOpenAIRequest kind;
	gchar *prompt;
} AssistantRequest;

static void assistant_start (EComposerOpenAI *panel, EOpenAIRequest kind, const gchar *prompt);

static void
request_free (AssistantRequest *request)
{
	g_object_unref (request->panel);
	g_free (request->prompt);
	g_free (request);
}

static void
assistant_busy (EComposerOpenAI *panel, gboolean busy)
{
	panel->busy = busy;
	gtk_widget_set_sensitive (panel->send, !busy);
	gtk_widget_set_sensitive (panel->prompt, !busy);
	gtk_widget_set_sensitive (panel->suggestions, !busy);
	gtk_widget_set_sensitive (panel->insert, !busy && panel->draft && *panel->draft);
	gtk_widget_set_sensitive (panel->stop, busy);
	if (busy)
		gtk_spinner_start (GTK_SPINNER (panel->spinner));
	else
		gtk_spinner_stop (GTK_SPINNER (panel->spinner));
}

static void
assistant_append (EComposerOpenAI *panel, gboolean user, const gchar *text)
{
	GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *message = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
	GtkWidget *author = gtk_label_new (user ? _("You") : e_openai_manager_get_provider_name (panel->manager));
	GtkWidget *body = gtk_label_new (text);
	gtk_widget_set_margin_start (row, user ? 28 : 0);
	gtk_widget_set_margin_end (row, user ? 0 : 28);
	gtk_widget_set_halign (message, user ? GTK_ALIGN_END : GTK_ALIGN_START);
	gtk_widget_set_hexpand (message, TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (message), user ? "mail-assistant-user" : "mail-assistant-reply");
	gtk_style_context_add_class (gtk_widget_get_style_context (author), "dim-label");
	gtk_label_set_xalign (GTK_LABEL (author), 0);
	gtk_label_set_xalign (GTK_LABEL (body), 0);
	gtk_label_set_selectable (GTK_LABEL (body), TRUE);
	gtk_label_set_line_wrap (GTK_LABEL (body), TRUE);
	gtk_label_set_line_wrap_mode (GTK_LABEL (body), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_max_width_chars (GTK_LABEL (body), 30);
	gtk_box_pack_start (GTK_BOX (message), author, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (message), body, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), message, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (panel->transcript), row, FALSE, FALSE, 0);
	gtk_widget_show_all (row);
}

static void
assistant_scroll_to_latest (GtkWidget *widget, GtkAllocation *allocation, EComposerOpenAI *panel)
{
	GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (panel->chat_scroll));
	gtk_adjustment_set_value (adjustment, MAX (0, gtk_adjustment_get_upper (adjustment) - gtk_adjustment_get_page_size (adjustment)));
}

static void
suggestion_clicked (GtkButton *button, EComposerOpenAI *panel)
{
	gtk_expander_set_expanded (GTK_EXPANDER (panel->ideas), FALSE);
	assistant_start (panel, E_OPENAI_DRAFT_REPLY, g_object_get_data (G_OBJECT (button), "reply-intention"));
}

static gboolean
assistant_suggestions (EComposerOpenAI *panel, const gchar *text)
{
	JsonNode *root = json_from_string (text, NULL);
	JsonNode *node;
	JsonArray *items;
	GList *children, *link;
	guint index;
	gboolean valid = FALSE;
	if (!root || !JSON_NODE_HOLDS_OBJECT (root))
		goto finished;
	node = json_object_get_member (json_node_get_object (root), "suggestions");
	if (!node || !JSON_NODE_HOLDS_ARRAY (node))
		goto finished;
	items = json_node_get_array (node);
	if (json_array_get_length (items) != 4)
		goto finished;
	for (index = 0; index < 4; index++) {
		JsonObject *item;
		JsonNode *label, *instruction;
		node = json_array_get_element (items, index);
		if (!JSON_NODE_HOLDS_OBJECT (node))
			goto finished;
		item = json_node_get_object (node);
		label = json_object_get_member (item, "label");
		instruction = json_object_get_member (item, "instruction");
		if (!label || !instruction || !JSON_NODE_HOLDS_VALUE (label) || !JSON_NODE_HOLDS_VALUE (instruction) ||
		    json_node_get_value_type (label) != G_TYPE_STRING || json_node_get_value_type (instruction) != G_TYPE_STRING ||
		    !*json_node_get_string (label) || !*json_node_get_string (instruction) ||
		    strlen (json_node_get_string (label)) > 160 || strlen (json_node_get_string (instruction)) > 2048)
			goto finished;
	}
	children = gtk_container_get_children (GTK_CONTAINER (panel->suggestions));
	for (link = children; link; link = g_list_next (link))
		gtk_widget_destroy (link->data);
	g_list_free (children);
	for (index = 0; index < 4; index++) {
		JsonObject *item = json_array_get_object_element (items, index);
		GtkWidget *button = gtk_button_new_with_label (json_object_get_string_member (item, "label"));
		GtkWidget *label = gtk_bin_get_child (GTK_BIN (button));
		gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
		gtk_label_set_max_width_chars (GTK_LABEL (label), 36);
		g_object_set_data_full (G_OBJECT (button), "reply-intention",
			g_strdup (json_object_get_string_member (item, "instruction")), g_free);
		gtk_widget_set_tooltip_text (button, json_object_get_string_member (item, "instruction"));
		g_signal_connect (button, "clicked", G_CALLBACK (suggestion_clicked), panel);
		gtk_box_pack_start (GTK_BOX (panel->suggestions), button, FALSE, FALSE, 0);
		gtk_widget_show_all (button);
	}
	valid = TRUE;
finished:
	if (root)
		json_node_free (root);
	return valid;
}

static void
assistant_response (GObject *source, GAsyncResult *result, gpointer data)
{
	AssistantRequest *request = data;
	EComposerOpenAI *panel = request->panel;
	GError *error = NULL;
	gchar *text = e_openai_manager_request_finish (E_OPENAI_MANAGER (source), result, &error);
	if (!panel->closed && request->serial == panel->serial) {
		if (error)
			gtk_label_set_text (GTK_LABEL (panel->status), error->message);
		else if (request->kind == E_OPENAI_SUGGEST_REPLIES) {
			gtk_label_set_text (GTK_LABEL (panel->status), assistant_suggestions (panel, text) ?
				_("Reply ideas") : _("The provider returned invalid reply ideas. Try refreshing."));
		} else {
			g_free (panel->draft);
			panel->draft = g_strdup (text);
			assistant_append (panel, FALSE, text);
			g_string_append_printf (panel->history, "User: %s\nDraft: %s\n", request->prompt, text);
			gtk_label_set_text (GTK_LABEL (panel->status), _("Draft ready"));
			gtk_entry_set_text (GTK_ENTRY (panel->prompt), "");
		}
		assistant_busy (panel, FALSE);
	}
	g_free (text);
	g_clear_error (&error);
	request_free (request);
}

static void
assistant_content (GObject *source, GAsyncResult *result, gpointer data)
{
	AssistantRequest *request = data;
	EComposerOpenAI *panel = request->panel;
	GError *error = NULL;
	EContentEditorContentHash *hash = e_content_editor_get_content_finish (E_CONTENT_EDITOR (source), result, &error);
	EMsgComposer *composer = g_weak_ref_get (&panel->composer);
	gchar *context = NULL;
	if (!panel->closed && composer && request->serial == panel->serial && hash) {
		const gchar *body = e_content_editor_util_get_content_data (hash, E_CONTENT_EDITOR_GET_TO_SEND_PLAIN);
		const gchar *subject = e_composer_header_table_get_subject (e_msg_composer_get_header_table (composer));
		if (request->kind != E_OPENAI_SUGGEST_REPLIES || (body && *body) || (subject && *subject))
			context = g_strdup_printf ("Subject: %s\n\n%s", subject ? subject : "", body ? body : "");
	}
	if (hash)
		e_content_editor_util_free_content_hash (hash);
	g_clear_object (&composer);
	if (context) {
		if (request->kind == E_OPENAI_DRAFT_REPLY)
			assistant_append (panel, TRUE, request->prompt);
		e_openai_manager_request (panel->manager, request->kind, context, request->prompt,
			panel->history->str, panel->cancellable, assistant_response, request);
		g_free (context);
	} else {
		if (!panel->closed && request->serial == panel->serial) {
			gtk_label_set_text (GTK_LABEL (panel->status), error ? error->message : _("Add email content or enter a request."));
			assistant_busy (panel, FALSE);
		}
		request_free (request);
	}
	g_clear_error (&error);
}

static void
assistant_start (EComposerOpenAI *panel, EOpenAIRequest kind, const gchar *prompt)
{
	EMsgComposer *composer;
	AssistantRequest *request;
	EOpenAIManager *selected;
	gchar *model;
	if (panel->busy || panel->closed)
		return;
	composer = g_weak_ref_get (&panel->composer);
	if (!composer)
		return;
	request = g_new0 (AssistantRequest, 1);
	request->prompt = g_strdup (prompt ? prompt : "");
	selected = e_openai_manager_ref_default ();
	model = e_openai_manager_dup_model (selected);
	if (g_strcmp0 (e_openai_manager_get_provider (panel->manager), e_openai_manager_get_provider (selected)) != 0 ||
	    g_strcmp0 (panel->model, model) != 0) {
		GList *children = gtk_container_get_children (GTK_CONTAINER (panel->transcript)), *link;
		for (link = children; link; link = g_list_next (link))
			gtk_widget_destroy (link->data);
		g_list_free (children);
		children = gtk_container_get_children (GTK_CONTAINER (panel->suggestions));
		for (link = children; link; link = g_list_next (link))
			gtk_widget_destroy (link->data);
		g_list_free (children);
		g_string_truncate (panel->history, 0);
		g_clear_pointer (&panel->draft, g_free);
	}
	g_set_object (&panel->manager, selected);
	g_object_unref (selected);
	g_free (panel->model);
	panel->model = model;
	g_clear_object (&panel->cancellable);
	panel->cancellable = g_cancellable_new ();
	request->panel = g_object_ref (panel);
	request->serial = ++panel->serial;
	request->kind = kind;
	assistant_busy (panel, TRUE);
	gtk_label_set_text (GTK_LABEL (panel->status), _("Contacting provider..."));
	e_content_editor_get_content (e_html_editor_get_content_editor (e_msg_composer_get_editor (composer)),
		E_CONTENT_EDITOR_GET_TO_SEND_PLAIN, NULL, panel->cancellable, assistant_content, request);
	g_object_unref (composer);
}

static void
assistant_stop (GtkButton *button, EComposerOpenAI *panel)
{
	panel->serial++;
	if (panel->cancellable)
		g_cancellable_cancel (panel->cancellable);
	assistant_busy (panel, FALSE);
	gtk_label_set_text (GTK_LABEL (panel->status), _("Cancelled"));
}

static void
assistant_send (GtkWidget *widget, EComposerOpenAI *panel)
{
	const gchar *prompt = gtk_entry_get_text (GTK_ENTRY (panel->prompt));
	if (*prompt)
		assistant_start (panel, E_OPENAI_DRAFT_REPLY, prompt);
}

static void
assistant_refresh (GtkButton *button, EComposerOpenAI *panel)
{
	gtk_expander_set_expanded (GTK_EXPANDER (panel->ideas), TRUE);
	assistant_start (panel, E_OPENAI_SUGGEST_REPLIES, NULL);
}

static void
assistant_insert (GtkButton *button, EComposerOpenAI *panel)
{
	EMsgComposer *composer = g_weak_ref_get (&panel->composer);
	if (composer && panel->draft && *panel->draft) {
		EContentEditor *editor = e_html_editor_get_content_editor (e_msg_composer_get_editor (composer));
		if (e_content_editor_is_editable (editor)) {
			e_content_editor_insert_content (editor, panel->draft,
				E_CONTENT_EDITOR_INSERT_TEXT_PLAIN | E_CONTENT_EDITOR_INSERT_CONVERT);
			gtk_label_set_text (GTK_LABEL (panel->status), _("Inserted into draft"));
		}
	}
	g_clear_object (&composer);
}

static void
assistant_settings (GtkButton *button, EComposerOpenAI *panel)
{
	EMsgComposer *composer = g_weak_ref_get (&panel->composer);
	if (composer) {
		GtkWidget *window = e_shell_get_preferences_window (e_msg_composer_get_shell (composer));
		e_preferences_window_setup (E_PREFERENCES_WINDOW (window));
		e_preferences_window_show_page (E_PREFERENCES_WINDOW (window), "page-integrations");
		gtk_window_present (GTK_WINDOW (window));
		g_object_unref (composer);
	}
}

static void
assistant_toggled (EUIAction *action, GParamSpec *spec, EComposerOpenAI *panel)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean open = g_variant_get_boolean (state);
	g_variant_unref (state);
	gtk_revealer_set_reveal_child (GTK_REVEALER (panel->revealer), open);
	if (open)
		assistant_start (panel, E_OPENAI_SUGGEST_REPLIES, NULL);
	else
		assistant_stop (NULL, panel);
}

static void assistant_close (GtkButton *button, EComposerOpenAI *panel)
{
	e_ui_action_set_state (panel->toggle, g_variant_new_boolean (FALSE));
}

static void assistant_destroy (GtkWidget *widget, EComposerOpenAI *panel)
{
	panel->closed = TRUE;
	panel->serial++;
	if (panel->cancellable)
		g_cancellable_cancel (panel->cancellable);
}

static void assistant_finalize (GObject *object)
{
	EComposerOpenAI *panel = (EComposerOpenAI *) object;
	g_weak_ref_clear (&panel->composer);
	g_clear_object (&panel->manager);
	g_clear_object (&panel->cancellable);
	g_clear_object (&panel->toggle);
	g_string_free (panel->history, TRUE);
	g_free (panel->draft);
	g_free (panel->model);
	G_OBJECT_CLASS (e_composer_openai_parent_class)->finalize (object);
}
static void e_composer_openai_class_init (EComposerOpenAIClass *class)
{
	G_OBJECT_CLASS (class)->finalize = assistant_finalize;
}
static void e_composer_openai_init (EComposerOpenAI *panel)
{
	g_weak_ref_init (&panel->composer, NULL);
	panel->manager = e_openai_manager_ref_default ();
	panel->history = g_string_new (NULL);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (panel), GTK_ORIENTATION_VERTICAL);
	gtk_box_set_spacing (GTK_BOX (panel), 8);
	gtk_container_set_border_width (GTK_CONTAINER (panel), 12);
	g_signal_connect (panel, "destroy", G_CALLBACK (assistant_destroy), panel);
}

static GtkWidget *
assistant_icon_button (const gchar *icon, const gchar *tooltip, GCallback callback, EComposerOpenAI *panel)
{
	GtkWidget *button = gtk_button_new_from_icon_name (icon, GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text (button, tooltip);
	atk_object_set_name (gtk_widget_get_accessible (button), tooltip);
	g_signal_connect (button, "clicked", callback, panel);
	return button;
}

void
e_composer_openai_attach (EMsgComposer *composer)
{
	static const EUIActionEntry entries[] = {
		{ "openai-assistant", "starred-symbolic", N_("Mail Assistant"), NULL,
		  N_("Open or close the mail assistant"), NULL, NULL, "false", (EUIActionFunc) e_ui_action_set_state }
	};
	EComposerOpenAI *panel = g_object_new (e_composer_openai_get_type (), NULL);
	GtkWidget *content = gtk_bin_get_child (GTK_BIN (composer));
	GtkWidget *layout = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *editor_scroll = gtk_scrolled_window_new (NULL, NULL);
	GtkWidget *row, *widget, *scroll;
	EUIManager *manager = e_html_editor_get_ui_manager (e_msg_composer_get_editor (composer));
	GtkCssProvider *style = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (style,
		".mail-assistant-user { background-color: alpha(@theme_selected_bg_color, 0.14); border: 1px solid alpha(@theme_selected_bg_color, 0.3); border-radius: 4px; padding: 10px; }"
		".mail-assistant-reply { background-color: @theme_base_color; border: 1px solid alpha(@theme_fg_color, 0.18); border-radius: 4px; padding: 10px; }", -1, NULL);
	gtk_style_context_add_provider_for_screen (gtk_widget_get_screen (GTK_WIDGET (composer)), GTK_STYLE_PROVIDER (style), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (style);
	g_weak_ref_set (&panel->composer, composer);
	g_object_ref (content);
	gtk_container_remove (GTK_CONTAINER (composer), content);
	gtk_container_add (GTK_CONTAINER (composer), layout);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (editor_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand (editor_scroll, TRUE);
	gtk_widget_set_vexpand (editor_scroll, TRUE);
	gtk_container_add (GTK_CONTAINER (editor_scroll), content);
	gtk_box_pack_start (GTK_BOX (layout), editor_scroll, TRUE, TRUE, 0);
	g_object_unref (content);
	panel->revealer = gtk_revealer_new ();
	gtk_widget_set_hexpand (panel->revealer, FALSE);
	gtk_revealer_set_transition_type (GTK_REVEALER (panel->revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
	gtk_container_add (GTK_CONTAINER (panel->revealer), GTK_WIDGET (panel));
	gtk_widget_set_size_request (GTK_WIDGET (panel), 340, -1);
	gtk_box_pack_end (GTK_BOX (layout), panel->revealer, FALSE, FALSE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	widget = gtk_label_new (_("Mail Assistant"));
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_xalign (GTK_LABEL (widget), 0);
	gtk_box_pack_start (GTK_BOX (row), widget, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (row), assistant_icon_button ("view-refresh-symbolic", _("Refresh reply ideas"), G_CALLBACK (assistant_refresh), panel), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), assistant_icon_button ("preferences-system-symbolic", _("Integrations settings"), G_CALLBACK (assistant_settings), panel), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), assistant_icon_button ("window-close-symbolic", _("Close assistant"), G_CALLBACK (assistant_close), panel), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (panel), row, FALSE, FALSE, 0);
	panel->suggestions = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	panel->ideas = gtk_expander_new (_("Reply ideas"));
	gtk_expander_set_expanded (GTK_EXPANDER (panel->ideas), TRUE);
	gtk_container_add (GTK_CONTAINER (panel->ideas), panel->suggestions);
	gtk_box_pack_start (GTK_BOX (panel), panel->ideas, FALSE, FALSE, 0);
	scroll = gtk_scrolled_window_new (NULL, NULL);
	panel->chat_scroll = scroll;
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	panel->transcript = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_valign (panel->transcript, GTK_ALIGN_START);
	g_signal_connect (panel->transcript, "size-allocate", G_CALLBACK (assistant_scroll_to_latest), panel);
	gtk_container_add (GTK_CONTAINER (scroll), panel->transcript);
	gtk_box_pack_start (GTK_BOX (panel), scroll, TRUE, TRUE, 0);
	panel->prompt = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY (panel->prompt), 2048);
	gtk_entry_set_placeholder_text (GTK_ENTRY (panel->prompt), _("Ask or refine the reply"));
	atk_object_set_name (gtk_widget_get_accessible (panel->prompt), _("Message to assistant"));
	gtk_box_pack_start (GTK_BOX (panel), panel->prompt, FALSE, FALSE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	panel->send = assistant_icon_button ("mail-send-symbolic", _("Generate reply"), G_CALLBACK (assistant_send), panel);
	panel->stop = assistant_icon_button ("process-stop-symbolic", _("Cancel request"), G_CALLBACK (assistant_stop), panel);
	panel->insert = gtk_button_new_with_label (_("Insert at Cursor"));
	gtk_button_set_image (GTK_BUTTON (panel->insert), gtk_image_new_from_icon_name ("insert-text-symbolic", GTK_ICON_SIZE_BUTTON));
	g_signal_connect (panel->insert, "clicked", G_CALLBACK (assistant_insert), panel);
	g_signal_connect (panel->prompt, "activate", G_CALLBACK (assistant_send), panel);
	gtk_box_pack_start (GTK_BOX (row), panel->send, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), panel->stop, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (row), panel->insert, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (panel), row, FALSE, FALSE, 0);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	panel->spinner = gtk_spinner_new ();
	panel->status = gtk_label_new ("");
	gtk_label_set_selectable (GTK_LABEL (panel->status), TRUE);
	gtk_label_set_line_wrap (GTK_LABEL (panel->status), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (panel->status), 34);
	gtk_label_set_xalign (GTK_LABEL (panel->status), 0);
	gtk_box_pack_start (GTK_BOX (row), panel->spinner, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), panel->status, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (panel), row, FALSE, FALSE, 0);
	e_ui_manager_add_actions (manager, "composer", GETTEXT_PACKAGE, entries, G_N_ELEMENTS (entries), composer);
	panel->toggle = g_object_ref (e_ui_manager_get_action (manager, "openai-assistant"));
	g_signal_connect_object (panel->toggle, "notify::state", G_CALLBACK (assistant_toggled), panel, 0);
	assistant_busy (panel, FALSE);
	gtk_widget_show_all (GTK_WIDGET (panel));
	gtk_widget_show (gtk_widget_get_parent (content));
	gtk_widget_show (editor_scroll);
	gtk_widget_show (panel->revealer);
	gtk_widget_show (layout);
}