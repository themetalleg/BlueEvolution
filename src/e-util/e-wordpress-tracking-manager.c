#include "evolution-config.h"
#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <libsecret/secret.h>
#include <camel/camel.h>
#include <string.h>
#include "e-wordpress-tracking-manager.h"

#define BLUEEVOLUTION_SETTINGS "io.github.themetalleg.BlueEvolution"
#define TRACKING_API_KEY_ACCOUNT "wordpress-tracking-project"
#define TRACKING_API_NAMESPACE "/wp-json/tracking-pixel/v1/projects/"

typedef enum {
	TRACKING_TASK_REGISTER,
	TRACKING_TASK_TEST,
	TRACKING_TASK_SUMMARY
} TrackingTaskKind;

typedef struct {
	TrackingTaskKind kind;
	gchar *site_url;
	gchar *project_id;
	gchar *external_id;
} TrackingTask;

struct _EWordPressTrackingManager {
	GObject parent;
	GSettings *settings;
	gchar *registered_external_id;
	gchar *registered_tracking_url;
	gchar *registered_project_id;
};

struct _EWordPressTrackingSummary {
	guint64 count;
	gchar *first_request;
	gchar *latest_request;
	GPtrArray *request_times;
	gchar *updated_at;
	gboolean history_complete;
};

static const SecretSchema tracking_key_schema = {
	"io.github.themetalleg.BlueEvolution.WordPressTracking", SECRET_SCHEMA_NONE,
	{ { "account", SECRET_SCHEMA_ATTRIBUTE_STRING }, { NULL, 0 } }
};

G_DEFINE_TYPE (EWordPressTrackingManager, e_wordpress_tracking_manager, G_TYPE_OBJECT)

static void
tracking_task_free (gpointer data)
{
	TrackingTask *task = data;
	g_free (task->site_url);
	g_free (task->project_id);
	g_free (task->external_id);
	g_free (task);
}

static void
tracking_summary_free (gpointer data)
{
	EWordPressTrackingSummary *summary = data;
	g_free (summary->first_request);
	g_free (summary->latest_request);
	g_clear_pointer (&summary->request_times, g_ptr_array_unref);
	g_free (summary->updated_at);
	g_free (summary);
}

static void
e_wordpress_tracking_manager_dispose (GObject *object)
{
	EWordPressTrackingManager *self = E_WORDPRESS_TRACKING_MANAGER (object);
	g_clear_object (&self->settings);
	g_clear_pointer (&self->registered_external_id, g_free);
	g_clear_pointer (&self->registered_tracking_url, g_free);
	g_clear_pointer (&self->registered_project_id, g_free);
	G_OBJECT_CLASS (e_wordpress_tracking_manager_parent_class)->dispose (object);
}

static void
e_wordpress_tracking_manager_class_init (EWordPressTrackingManagerClass *class)
{
	G_OBJECT_CLASS (class)->dispose = e_wordpress_tracking_manager_dispose;
}

static void
e_wordpress_tracking_manager_init (EWordPressTrackingManager *self)
{
	self->settings = g_settings_new (BLUEEVOLUTION_SETTINGS);
}

EWordPressTrackingManager *
e_wordpress_tracking_manager_new (void)
{
	return g_object_new (E_TYPE_WORDPRESS_TRACKING_MANAGER, NULL);
}

gboolean
e_wordpress_tracking_manager_get_enabled (EWordPressTrackingManager *self)
{
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), FALSE);
	return g_settings_get_boolean (self->settings, "mail-tracking-enabled");
}

void
e_wordpress_tracking_manager_set_enabled (EWordPressTrackingManager *self,
                                           gboolean enabled)
{
	g_return_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self));
	g_settings_set_boolean (self->settings, "mail-tracking-enabled", enabled);
	if (!enabled)
		g_settings_set_boolean (self->settings, "mail-tracking-default", FALSE);
}

gboolean
e_wordpress_tracking_manager_get_default (EWordPressTrackingManager *self)
{
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), FALSE);
	return g_settings_get_boolean (self->settings, "mail-tracking-default");
}

void
e_wordpress_tracking_manager_set_default (EWordPressTrackingManager *self,
                                            gboolean enabled)
{
	g_return_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self));
	g_settings_set_boolean (self->settings, "mail-tracking-default", enabled &&
		e_wordpress_tracking_manager_get_enabled (self));
}

gchar *
e_wordpress_tracking_manager_dup_site_url (EWordPressTrackingManager *self)
{
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), NULL);
	return g_settings_get_string (self->settings, "mail-tracking-site-url");
}

void
e_wordpress_tracking_manager_set_site_url (EWordPressTrackingManager *self,
                                             const gchar *site_url)
{
	g_return_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self));
	g_settings_set_string (self->settings, "mail-tracking-site-url", site_url ? site_url : "");
}

gchar *
e_wordpress_tracking_manager_dup_project_id (EWordPressTrackingManager *self)
{
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), NULL);
	return g_settings_get_string (self->settings, "mail-tracking-project-id");
}

void
e_wordpress_tracking_manager_set_project_id (EWordPressTrackingManager *self,
                                               const gchar *project_id)
{
	g_return_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self));
	g_settings_set_string (self->settings, "mail-tracking-project-id", project_id ? project_id : "");
}

static gboolean
tracking_valid_chars (const gchar *value,
                      gsize expected_length)
{
	gsize ii;
	if (!value || strlen (value) != expected_length)
		return FALSE;
	for (ii = 0; ii < expected_length; ii++) {
		if (!g_ascii_isalnum (value[ii]))
			return FALSE;
	}
	return TRUE;
}

static gchar *
tracking_normalize_site_url (const gchar *value,
                             GError **error)
{
	GUri *uri;
	const gchar *scheme, *host, *userinfo, *query, *fragment;
	gchar *normalized;
	gsize length;

	uri = g_uri_parse (value ? value : "", G_URI_FLAGS_NONE, error);
	if (!uri)
		return NULL;
	scheme = g_uri_get_scheme (uri);
	host = g_uri_get_host (uri);
	userinfo = g_uri_get_userinfo (uri);
	query = g_uri_get_query (uri);
	fragment = g_uri_get_fragment (uri);
	if (g_ascii_strcasecmp (scheme ? scheme : "", "https") != 0 || !host || !*host ||
	    userinfo || query || fragment) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			_("Enter an HTTPS WordPress home URL without credentials, query, or fragment."));
		g_uri_unref (uri);
		return NULL;
	}
	normalized = g_uri_to_string_partial (uri, G_URI_HIDE_PASSWORD | G_URI_HIDE_FRAGMENT);
	g_uri_unref (uri);
	length = strlen (normalized);
	while (length > strlen ("https://") && normalized[length - 1] == '/')
		normalized[--length] = '\0';
	return normalized;
}

gboolean
e_wordpress_tracking_manager_validate_configuration (const gchar *site_url,
                                                     const gchar *project_id,
                                                     GError **error)
{
	gchar *normalized;
	normalized = tracking_normalize_site_url (site_url, error);
	if (!normalized)
		return FALSE;
	g_free (normalized);
	if (!tracking_valid_chars (project_id, 10)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			_("Enter a project ID containing exactly 10 ASCII letters or digits."));
		return FALSE;
	}
	return TRUE;
}

static gchar *
tracking_lookup_key (GError **error)
{
	return secret_password_lookup_sync (&tracking_key_schema, NULL, error,
		"account", TRACKING_API_KEY_ACCOUNT, NULL);
}

gboolean
e_wordpress_tracking_manager_has_key (EWordPressTrackingManager *self,
                                      GError **error)
{
	gchar *key;
	gboolean result;
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), FALSE);
	key = tracking_lookup_key (error);
	result = key && *key;
	secret_password_free (key);
	return result;
}

gboolean
e_wordpress_tracking_manager_store_key (EWordPressTrackingManager *self,
                                         const gchar *key,
                                         GError **error)
{
	gboolean result;
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), FALSE);
	if (!key || !g_regex_match_simple ("\\Awtp_[A-Za-z0-9]{16}\\.[A-Za-z0-9]{64}\\z", key, 0, 0)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			_("The project API key has an invalid format."));
		return FALSE;
	}
	result = secret_password_store_sync (&tracking_key_schema, SECRET_COLLECTION_DEFAULT,
		_("BlueEvolution WordPress Tracking API key"), key, NULL, error,
		"account", TRACKING_API_KEY_ACCOUNT, NULL);
	return result;
}

gboolean
e_wordpress_tracking_manager_remove_key (EWordPressTrackingManager *self,
                                          GError **error)
{
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), FALSE);
	return secret_password_clear_sync (&tracking_key_schema, NULL, error,
		"account", TRACKING_API_KEY_ACCOUNT, NULL);
}

gboolean
e_wordpress_tracking_manager_is_configured (EWordPressTrackingManager *self,
                                             GError **error)
{
	gchar *site_url, *normalized, *project_id, *key;
	gboolean configured;
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), FALSE);
	site_url = e_wordpress_tracking_manager_dup_site_url (self);
	project_id = e_wordpress_tracking_manager_dup_project_id (self);
	normalized = tracking_normalize_site_url (site_url, error);
	g_free (site_url);
	if (!normalized) {
		g_free (project_id);
		return FALSE;
	}
	g_free (normalized);
	configured = tracking_valid_chars (project_id, 10);
	g_free (project_id);
	if (!configured) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			_("Enter a project ID containing exactly 10 ASCII letters or digits."));
		return FALSE;
	}
	key = tracking_lookup_key (error);
	configured = key && g_regex_match_simple ("\\Awtp_[A-Za-z0-9]{16}\\.[A-Za-z0-9]{64}\\z", key, 0, 0);
	secret_password_free (key);
	if (!configured && (!error || !*error))
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			_("Store a valid project API key in Integrations settings first."));
	return configured;
}

gchar *
e_wordpress_tracking_manager_generate_external_id (void)
{
	GString *external_id = g_string_sized_new (128);
	guint ii;
	for (ii = 0; ii < 4; ii++) {
		gchar *uuid = g_uuid_string_random ();
		const gchar *ptr;
		for (ptr = uuid; *ptr; ptr++) {
			if (*ptr != '-')
				g_string_append_c (external_id, *ptr);
		}
		g_free (uuid);
	}
	g_assert (external_id->len == 128);
	return g_string_free (external_id, FALSE);
}

static gchar *
tracking_base_url (TrackingTask *task)
{
	gchar *site_url = tracking_normalize_site_url (task->site_url, NULL);
	gchar *url = g_strdup_printf ("%s%s%s", site_url, TRACKING_API_NAMESPACE, task->project_id);
	g_free (site_url);
	return url;
}

static JsonNode *
tracking_request (SoupSession *session,
                 const gchar *url,
                 const gchar *method,
                 const gchar *key,
                 const gchar *json_body,
                 guint *out_status,
                 GCancellable *cancellable,
                 GError **error)
{
	SoupMessage *message;
	GBytes *response;
	const gchar *response_data;
	gsize response_length;
	JsonParser *parser;
	JsonNode *root = NULL;
	guint status;
	message = soup_message_new (method, url);
	if (!message) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			_("The WordPress API URL is invalid."));
		return NULL;
	}
	soup_message_set_flags (message, SOUP_MESSAGE_NO_REDIRECT);
	soup_message_headers_replace (soup_message_get_request_headers (message), "Authorization", key);
	soup_message_headers_replace (soup_message_get_request_headers (message), "Accept", "application/json");
	if (json_body) {
		GBytes *body = g_bytes_new (json_body, strlen (json_body));
		soup_message_set_request_body_from_bytes (message, "application/json", body);
		g_bytes_unref (body);
	}
	response = soup_session_send_and_read (session, message, cancellable, error);
	status = soup_message_get_status (message);
	if (out_status)
		*out_status = status;
	g_object_unref (message);
	if (!response)
		return NULL;
	response_data = g_bytes_get_data (response, &response_length);
	parser = json_parser_new ();
	if (response_length > 1024 * 1024 || !json_parser_load_from_data (parser, response_data, response_length, error)) {
		if (!error || !*error)
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				_("WordPress returned an invalid response."));
	} else {
		root = json_node_copy (json_parser_get_root (parser));
	}
	g_object_unref (parser);
	g_bytes_unref (response);
	return root;
}

static const gchar *
tracking_json_string (JsonObject *object,
                     const gchar *name)
{
	JsonNode *node = json_object_get_member (object, name);
	return node && JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING ?
		json_node_get_string (node) : NULL;
}

static gboolean
tracking_url_matches (const gchar *site_url,
                      const gchar *tracking_url,
                      const gchar *project_id,
                      GError **error)
{
	GUri *site, *pixel;
	GHashTable *params;
	const gchar *site_host, *pixel_host, *query, *token_value;
	gchar *site_path, *pixel_path, *expected;
	guint site_port, pixel_port;
	gboolean valid;
	if (!tracking_url || !*tracking_url) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			_("WordPress returned a tracking URL outside the configured project."));
		return FALSE;
	}
	site = g_uri_parse (site_url, G_URI_FLAGS_NONE, error);
	pixel = g_uri_parse (tracking_url, G_URI_FLAGS_NONE, error);
	if (!site || !pixel) {
		g_clear_pointer (&site, g_uri_unref);
		g_clear_pointer (&pixel, g_uri_unref);
		return FALSE;
	}
	site_host = g_uri_get_host (site);
	pixel_host = g_uri_get_host (pixel);
	site_port = g_uri_get_port (site) < 0 ? 443 : g_uri_get_port (site);
	pixel_port = g_uri_get_port (pixel) < 0 ? 443 : g_uri_get_port (pixel);
	site_path = g_strdup (g_uri_get_path (site) ? g_uri_get_path (site) : "");
	pixel_path = g_strdup (g_uri_get_path (pixel) ? g_uri_get_path (pixel) : "");
	while (*site_path && site_path[strlen (site_path) - 1] == '/')
		site_path[strlen (site_path) - 1] = '\0';
	while (*pixel_path && pixel_path[strlen (pixel_path) - 1] == '/')
		pixel_path[strlen (pixel_path) - 1] = '\0';
	query = g_uri_get_query (pixel);
	params = query ? g_uri_parse_params (query, -1, "&", G_URI_PARAMS_NONE, NULL) : NULL;
	token_value = params ? g_hash_table_lookup (params, "wtp_pixel") : NULL;
	expected = token_value ? g_strdup_printf ("%s.", project_id) : NULL;
	valid = g_ascii_strcasecmp (g_uri_get_scheme (pixel) ? g_uri_get_scheme (pixel) : "", "https") == 0 &&
		site_host && pixel_host && g_ascii_strcasecmp (site_host, pixel_host) == 0 &&
		site_port == pixel_port && g_strcmp0 (site_path, pixel_path) == 0 &&
		!g_uri_get_userinfo (pixel) && !g_uri_get_fragment (pixel) && token_value &&
		g_str_has_prefix (token_value, expected) && strlen (token_value + strlen (expected)) == 64 &&
		tracking_valid_chars (token_value + strlen (expected), 64);
	if (!valid)
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			_("WordPress returned a tracking URL outside the configured project."));
	g_free (expected);
	g_free (site_path);
	g_free (pixel_path);
	if (params)
		g_hash_table_unref (params);
	g_uri_unref (site);
	g_uri_unref (pixel);
	return valid;
}

static gboolean
tracking_registration_matches (JsonObject *object,
			       const gchar *site_url,
			       const gchar *project_id,
			       const gchar *external_id,
			       GError **error)
{
	if (g_strcmp0 (tracking_json_string (object, "project_id"), project_id) != 0 ||
	    g_strcmp0 (tracking_json_string (object, "external_id"), external_id) != 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			_("WordPress returned a mismatched project or external email ID."));
		return FALSE;
	}
	return tracking_url_matches (site_url, tracking_json_string (object, "tracking_url"), project_id, error);
}

static gchar *
tracking_history_path (const gchar *site_url,
		       const gchar *project_id,
		       const gchar *external_id)
{
	gchar *identity = g_strdup_printf ("%s\n%s", site_url, project_id);
	gchar *digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, identity, -1);
	gchar *filename = g_strconcat (external_id, ".json", NULL);
	gchar *path = g_build_filename (g_get_user_data_dir (), "blueevolution", "tracking-history", digest, filename, NULL);
	g_free (identity);
	g_free (digest);
	g_free (filename);
	return path;
}

static gboolean
tracking_history_save (TrackingTask *data,
		       EWordPressTrackingSummary *summary,
		       GError **error)
{
	JsonBuilder *builder = json_builder_new ();
	JsonGenerator *generator = json_generator_new ();
	JsonNode *root;
	gchar *path = tracking_history_path (data->site_url, data->project_id, data->external_id);
	gchar *directory = g_path_get_dirname (path);
	gchar *contents;
	guint index;
	gboolean saved;
	if (g_mkdir_with_parents (directory, 0700) != 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Tracking history could not be saved locally."));
		saved = FALSE;
		goto exit;
	}
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "count");
	json_builder_add_int_value (builder, summary->count);
	json_builder_set_member_name (builder, "updated_at");
	json_builder_add_string_value (builder, summary->updated_at);
	json_builder_set_member_name (builder, "history_complete");
	json_builder_add_boolean_value (builder, summary->history_complete);
	json_builder_set_member_name (builder, "requests");
	json_builder_begin_array (builder);
	for (index = 0; index < summary->request_times->len; index++)
		json_builder_add_string_value (builder, g_ptr_array_index (summary->request_times, index));
	json_builder_end_array (builder);
	json_builder_end_object (builder);
	root = json_builder_get_root (builder);
	json_generator_set_root (generator, root);
	contents = json_generator_to_data (generator, NULL);
	saved = g_file_set_contents_full (path, contents, -1,
		G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
	g_free (contents);
	json_node_free (root);
exit:
	g_free (directory);
	g_free (path);
	g_object_unref (generator);
	g_object_unref (builder);
	return saved;
}

static gchar *
tracking_get_key (GError **error)
{
	return secret_password_lookup_sync (&tracking_key_schema, NULL, error,
		"account", TRACKING_API_KEY_ACCOUNT, NULL);
}

static void
tracking_task_thread (GTask *task,
                      gpointer source_object,
                      gpointer task_data,
                      GCancellable *cancellable)
{
	EWordPressTrackingManager *self = source_object;
	TrackingTask *data = task_data;
	SoupSession *session = NULL;
	JsonNode *root = NULL;
	JsonObject *object;
	gchar *key = NULL, *base_url = NULL, *url = NULL, *payload = NULL;
	gchar *authorization = NULL;
	gchar *snapshot_at = NULL, *snapshot_query = NULL;
	guint status = 0;
	GError *error = NULL;
	g_assert (E_IS_WORDPRESS_TRACKING_MANAGER (self));
	if (g_task_return_error_if_cancelled (task))
		return;
	key = tracking_get_key (&error);
	if (error || !key || !*key)
		goto failed;
	authorization = g_strdup_printf ("Bearer %s", key);
	base_url = tracking_base_url (data);
	session = soup_session_new_with_options ("timeout", 15, NULL);
	if (data->kind == TRACKING_TASK_REGISTER) {
		url = g_strconcat (base_url, "/messages", NULL);
		payload = g_strdup_printf ("{\"external_id\":\"%s\"}", data->external_id);
		root = tracking_request (session, url, "POST", authorization, payload, &status, cancellable, &error);
		if (!root || status < 200 || status >= 300 || !JSON_NODE_HOLDS_OBJECT (root))
			goto failed;
		object = json_node_get_object (root);
		{
			const gchar *response_url = tracking_json_string (object, "tracking_url");
			if (!tracking_registration_matches (object, data->site_url, data->project_id, data->external_id, &error)) {
				goto failed;
			}
			g_task_return_pointer (task, g_strdup (response_url), g_free);
		}
	} else if (data->kind == TRACKING_TASK_SUMMARY) {
		GDateTime *now = g_date_time_new_now_utc ();
		snapshot_at = g_date_time_format (now, "%Y-%m-%dT%H:%M:%SZ");
		g_date_time_unref (now);
		snapshot_query = g_uri_escape_string (snapshot_at, NULL, FALSE);
		url = g_strdup_printf ("%s/summary?external_id=%s&to=%s", base_url, data->external_id, snapshot_query);
		root = tracking_request (session, url, "GET", authorization, NULL, &status, cancellable, &error);
		if (!root || status < 200 || status >= 300 || !JSON_NODE_HOLDS_OBJECT (root))
			goto failed;
		object = json_node_get_object (root);
		if (g_strcmp0 (tracking_json_string (object, "project_id"), data->project_id) != 0 ||
		    !json_object_has_member (object, "count")) {
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				_("WordPress returned a summary for a different project."));
			goto failed;
		}
		{
			EWordPressTrackingSummary *summary = g_new0 (EWordPressTrackingSummary, 1);
			JsonNode *count = json_object_get_member (object, "count");
			gint64 count_value;
			if (!JSON_NODE_HOLDS_VALUE (count) || json_node_get_value_type (count) != G_TYPE_INT64 ||
			    (count_value = json_node_get_int (count)) < 0) {
				g_free (summary);
				g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("WordPress returned an invalid opening count."));
				goto failed;
			}
			summary->count = count_value;
			summary->first_request = g_strdup (tracking_json_string (object, "first_request"));
			summary->latest_request = g_strdup (tracking_json_string (object, "latest_request"));
			summary->request_times = g_ptr_array_new_with_free_func (g_free);
			{
				guint page, pages = MIN ((summary->count + 99) / 100, 100);
				summary->updated_at = g_strdup (snapshot_at);
				for (page = 1; page <= pages; page++) {
					JsonArray *events;
					guint index;
					json_node_free (root);
					root = NULL;
					g_free (url);
					url = g_strdup_printf ("%s/events?external_id=%s&per_page=100&page=%u&to=%s", base_url, data->external_id, page, snapshot_query);
					root = tracking_request (session, url, "GET", authorization, NULL, &status, cancellable, &error);
					if (!root || status < 200 || status >= 300 || !JSON_NODE_HOLDS_OBJECT (root)) {
						tracking_summary_free (summary);
						goto failed;
					}
					object = json_node_get_object (root);
					if (g_strcmp0 (tracking_json_string (object, "project_id"), data->project_id) != 0 ||
					    !json_object_has_member (object, "events") ||
					    !JSON_NODE_HOLDS_ARRAY (json_object_get_member (object, "events"))) {
						g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("WordPress returned invalid tracking history."));
						tracking_summary_free (summary);
						goto failed;
					}
					events = json_object_get_array_member (object, "events");
					for (index = 0; index < json_array_get_length (events); index++) {
						JsonNode *event_node = json_array_get_element (events, index);
						JsonObject *event = JSON_NODE_HOLDS_OBJECT (event_node) ? json_node_get_object (event_node) : NULL;
						const gchar *time = event ? tracking_json_string (event, "requested_at") : NULL;
						GDateTime *parsed = time ? g_date_time_new_from_iso8601 (time, NULL) : NULL;
						if (!parsed || g_strcmp0 (tracking_json_string (event, "external_id"), data->external_id) != 0) {
							g_clear_pointer (&parsed, g_date_time_unref);
							g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("WordPress returned invalid tracking history."));
							tracking_summary_free (summary);
							goto failed;
						}
						g_date_time_unref (parsed);
						g_ptr_array_add (summary->request_times, g_strdup (time));
					}
				}
				summary->history_complete = summary->request_times->len == summary->count;
			}
			if (!tracking_history_save (data, summary, &error)) {
				tracking_summary_free (summary);
				goto failed;
			}
			g_task_return_pointer (task, summary, tracking_summary_free);
		}
	} else {
		gchar *probe_url = g_strconcat (base_url, "/summary", NULL);
		root = tracking_request (session, probe_url, "GET", authorization, NULL, &status, cancellable, &error);
		g_free (probe_url);
		if (!root || status < 200 || status >= 300 || !JSON_NODE_HOLDS_OBJECT (root) ||
		    g_strcmp0 (tracking_json_string (json_node_get_object (root), "project_id"), data->project_id) != 0) {
			if (!error)
				g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
					_("WordPress summary check failed (HTTP %u)."), status);
			goto failed;
		}
		json_node_free (root);
		root = NULL;
		g_free (url);
		url = g_strconcat (base_url, "/messages/resolve", NULL);
		payload = g_strdup_printf ("{\"token\":\"%s\"}", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
		root = tracking_request (session, url, "POST", authorization, payload, &status, cancellable, &error);
		if (error)
			goto failed;
		if (!root || !JSON_NODE_HOLDS_OBJECT (root) ||
		    !((status == 404 &&
		       g_strcmp0 (tracking_json_string (json_node_get_object (root), "code"), "wtp_message_not_found") == 0) ||
		      (status >= 200 && status < 300 &&
		       g_strcmp0 (tracking_json_string (json_node_get_object (root), "project_id"), data->project_id) == 0))) {
			g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
				_("WordPress token resolver check failed (HTTP %u)."), status);
			goto failed;
		}
		json_node_free (root);
		root = NULL;
		g_free (url);
		url = g_strconcat (base_url, "/messages", NULL);
		g_free (payload);
		{
			gchar *external_id = g_strnfill (128, '0');
			const gchar *prefix = "BlueEvolutionConnectionTest";
			memcpy (external_id, prefix, strlen (prefix));
			payload = g_strdup_printf ("{\"external_id\":\"%s\"}", external_id);
			root = tracking_request (session, url, "POST", authorization, payload, &status, cancellable, &error);
			if (!root || status < 200 || status >= 300 || !JSON_NODE_HOLDS_OBJECT (root) ||
			    !tracking_registration_matches (json_node_get_object (root), data->site_url, data->project_id, external_id, &error)) {
				g_free (external_id);
				goto failed;
			}
			g_free (external_id);
		}
		g_task_return_boolean (task, TRUE);
	}
	goto finished;
failed:
	if (!error)
		g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, _("WordPress request failed (HTTP %u)."), status);
	g_task_return_error (task, error);
finished:
	if (root)
		json_node_free (root);
	g_clear_object (&session);
	secret_password_free (key);
	g_free (authorization);
	g_free (base_url);
	g_free (url);
	g_free (payload);
	g_free (snapshot_at);
	g_free (snapshot_query);
}

static void
tracking_start_task (EWordPressTrackingManager *self,
                     TrackingTaskKind kind,
                     const gchar *external_id,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GTask *task;
	TrackingTask *data;
	GError *error = NULL;
	gchar *site_url, *project_id, *normalized;
	g_return_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self));
	data = g_new0 (TrackingTask, 1);
	data->kind = kind;
	site_url = e_wordpress_tracking_manager_dup_site_url (self);
	project_id = e_wordpress_tracking_manager_dup_project_id (self);
	normalized = tracking_normalize_site_url (site_url, &error);
	g_free (site_url);
	if (!normalized || !tracking_valid_chars (project_id, 10) ||
	    ((kind == TRACKING_TASK_REGISTER || kind == TRACKING_TASK_SUMMARY) &&
	     !tracking_valid_chars (external_id, 128))) {
		if (!error)
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				_("Complete the HTTPS site URL, project ID, and email ID in Integrations settings."));
		g_free (normalized);
		g_free (project_id);
		tracking_task_free (data);
		task = g_task_new (self, cancellable, callback, user_data);
		g_task_return_error (task, error);
		g_object_unref (task);
		return;
	}
	data->site_url = normalized;
	data->project_id = project_id;
	data->external_id = g_strdup (external_id);
	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_task_data (task, data, tracking_task_free);
	g_task_run_in_thread (task, tracking_task_thread);
	g_object_unref (task);
}

void
e_wordpress_tracking_manager_register_async (EWordPressTrackingManager *self,
                                               const gchar *external_id,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
	tracking_start_task (self, TRACKING_TASK_REGISTER, external_id, cancellable, callback, user_data);
}

gchar *
e_wordpress_tracking_manager_register_finish (EWordPressTrackingManager *self,
                                                GAsyncResult *result,
                                                GError **error)
{
	TrackingTask *task;
	gchar *tracking_url;
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	tracking_url = g_task_propagate_pointer (G_TASK (result), error);
	if (tracking_url) {
		task = g_task_get_task_data (G_TASK (result));
		if (!task || !task->external_id) {
			g_free (tracking_url);
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				_("WordPress registration returned without an email ID."));
			return NULL;
		}
		g_free (self->registered_external_id);
		g_free (self->registered_tracking_url);
		g_free (self->registered_project_id);
		self->registered_external_id = g_strdup (task->external_id);
		self->registered_tracking_url = g_strdup (tracking_url);
		self->registered_project_id = g_strdup (task->project_id);
	}
	return tracking_url;
}

void
e_wordpress_tracking_manager_test_async (EWordPressTrackingManager *self,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
	tracking_start_task (self, TRACKING_TASK_TEST, NULL, cancellable, callback, user_data);
}

gboolean
e_wordpress_tracking_manager_test_finish (EWordPressTrackingManager *self,
                                            GAsyncResult *result,
                                            GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
	return g_task_propagate_boolean (G_TASK (result), error);
}

void
e_wordpress_tracking_manager_summary_async (EWordPressTrackingManager *self,
                                               const gchar *external_id,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
	tracking_start_task (self, TRACKING_TASK_SUMMARY, external_id, cancellable, callback, user_data);
}

EWordPressTrackingSummary *
e_wordpress_tracking_manager_summary_finish (EWordPressTrackingManager *self,
                                               GAsyncResult *result,
                                               GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	return g_task_propagate_pointer (G_TASK (result), error);
}

void
e_wordpress_tracking_summary_free (EWordPressTrackingSummary *summary)
{
	tracking_summary_free (summary);
}

EWordPressTrackingSummary *
e_wordpress_tracking_manager_load_history (EWordPressTrackingManager *self,
					 const gchar *external_id,
					 GError **error)
{
	gchar *site = e_wordpress_tracking_manager_dup_site_url (self);
	gchar *project = e_wordpress_tracking_manager_dup_project_id (self);
	gchar *normalized = tracking_normalize_site_url (site, error);
	gchar *path = NULL;
	JsonParser *parser = json_parser_new ();
	EWordPressTrackingSummary *summary = NULL;
	JsonObject *object;
	JsonArray *requests;
	JsonNode *count;
	guint index;
	if (!normalized || !tracking_valid_chars (project, 10) || !tracking_valid_chars (external_id, 128))
		goto exit;
	path = tracking_history_path (normalized, project, external_id);
	if (!g_file_test (path, G_FILE_TEST_EXISTS) || !json_parser_load_from_file (parser, path, error))
		goto exit;
	if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
		goto invalid;
	object = json_node_get_object (json_parser_get_root (parser));
	count = json_object_get_member (object, "count");
	if (!count || !JSON_NODE_HOLDS_VALUE (count) || json_node_get_value_type (count) != G_TYPE_INT64 ||
	    json_node_get_int (count) < 0 || !tracking_json_string (object, "updated_at") ||
	    !json_object_has_member (object, "requests") || !JSON_NODE_HOLDS_ARRAY (json_object_get_member (object, "requests")))
		goto invalid;
	requests = json_object_get_array_member (object, "requests");
	summary = g_new0 (EWordPressTrackingSummary, 1);
	summary->count = json_node_get_int (count);
	summary->updated_at = g_strdup (tracking_json_string (object, "updated_at"));
	summary->request_times = g_ptr_array_new_with_free_func (g_free);
	for (index = 0; index < json_array_get_length (requests); index++) {
		JsonNode *node = json_array_get_element (requests, index);
		const gchar *time = JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING ? json_node_get_string (node) : NULL;
		GDateTime *parsed = time ? g_date_time_new_from_iso8601 (time, NULL) : NULL;
		if (!parsed) {
			tracking_summary_free (summary);
			summary = NULL;
			goto invalid;
		}
		g_date_time_unref (parsed);
		g_ptr_array_add (summary->request_times, g_strdup (time));
	}
	summary->history_complete = summary->request_times->len == summary->count;
	goto exit;
invalid:
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Saved tracking history is invalid."));
exit:
	g_free (site);
	g_free (project);
	g_free (normalized);
	g_free (path);
	g_object_unref (parser);
	return summary;
}

const GPtrArray *
e_wordpress_tracking_summary_get_request_times (const EWordPressTrackingSummary *summary)
{
	return summary ? summary->request_times : NULL;
}

const gchar *
e_wordpress_tracking_summary_get_updated_at (const EWordPressTrackingSummary *summary)
{
	return summary ? summary->updated_at : NULL;
}

gboolean
e_wordpress_tracking_summary_get_history_complete (const EWordPressTrackingSummary *summary)
{
	return summary && summary->history_complete;
}

guint64
e_wordpress_tracking_summary_get_count (const EWordPressTrackingSummary *summary)
{
	return summary ? summary->count : 0;
}

const gchar *
e_wordpress_tracking_summary_get_first_request (const EWordPressTrackingSummary *summary)
{
	return summary ? summary->first_request : NULL;
}

const gchar *
e_wordpress_tracking_summary_get_latest_request (const EWordPressTrackingSummary *summary)
{
	return summary ? summary->latest_request : NULL;
}

static GRegex *
tracking_pixel_regex (void)
{
	static GRegex *regex;
	static gsize initialized;
	if (g_once_init_enter (&initialized)) {
		GError *error = NULL;
		GRegex *compiled = g_regex_new (
			"(?is)<img\\b(?=[^>]*\\bsrc\\s*=\\s*(?:\"[^\"]*wtp_pixel=[A-Za-z0-9]{10}\\.[A-Za-z0-9]{64}[^\"]*\"|'[^']*wtp_pixel=[A-Za-z0-9]{10}\\.[A-Za-z0-9]{64}[^']*'))[^>]*>",
			G_REGEX_OPTIMIZE, 0, &error);
		g_assert_no_error (error);
		regex = compiled;
		g_once_init_leave (&initialized, 1);
	}
	return regex;
}

static gboolean
tracking_prepare_part (EWordPressTrackingManager *self,
		       CamelMimePart *part,
		       gboolean track_openings,
		       gboolean *found_html,
		       gboolean *inserted_pixel,
		       GError **error)
{
	CamelDataWrapper *content;
	CamelContentType *content_type;
	const gchar *disposition;
	disposition = camel_mime_part_get_disposition (part);
	if (disposition && g_ascii_strcasecmp (disposition, "attachment") == 0)
		return TRUE;
	content = camel_medium_get_content (CAMEL_MEDIUM (part));
	content_type = camel_mime_part_get_content_type (part);
	if (content_type && camel_content_type_is (content_type, "text", "html")) {
		CamelStream *stream = camel_stream_mem_new ();
		GByteArray *decoded;
		gchar *html, *clean;
		GString *output;
		if (content && camel_data_wrapper_decode_to_stream_sync (content, stream, NULL, error) < 0) {
			g_object_unref (stream);
			return FALSE;
		}
		decoded = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));
		html = decoded ? g_strndup ((const gchar *) decoded->data, decoded->len) : g_strdup ("");
		g_object_unref (stream);
		clean = g_regex_replace (tracking_pixel_regex (), html, -1, 0, "", 0, error);
		g_free (html);
		if (!clean)
			return FALSE;
		output = g_string_new (clean);
		g_free (clean);
		*found_html = TRUE;
		if (track_openings && !*inserted_pixel) {
			gchar *pixel_url = g_strdup_printf ("%s&wtp_external_id=%s",
				self->registered_tracking_url, self->registered_external_id);
			gchar *escaped_url = g_markup_escape_text (pixel_url, -1);
			g_string_append_printf (output,
				"<img src=\"%s\" width=\"1\" height=\"1\" alt=\"\" style=\"border:0\" />",
				escaped_url);
			g_free (escaped_url);
			g_free (pixel_url);
			*inserted_pixel = TRUE;
		}
		camel_mime_part_set_content (part, output->str, output->len, "text/html; charset=utf-8");
		camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
		g_string_free (output, TRUE);
		return TRUE;
	}
	if (content && CAMEL_IS_MULTIPART (content)) {
		CamelMultipart *multipart = CAMEL_MULTIPART (content);
		gint ii;
		for (ii = 0; ii < camel_multipart_get_number (multipart); ii++) {
			CamelMimePart *child = camel_multipart_get_part (multipart, ii);
			if (child && !tracking_prepare_part (self, child, track_openings, found_html, inserted_pixel, error))
				return FALSE;
		}
	}
	return TRUE;
}

gboolean
e_wordpress_tracking_manager_prepare_message (EWordPressTrackingManager *self,
					     CamelMimeMessage *message,
					     gboolean track_openings,
					     GError **error)
{
	gboolean found_html = FALSE, inserted_pixel = FALSE;
	gchar *header = NULL, *configured_site = NULL, *configured_project = NULL;
	CamelContentType *content_type;
	g_return_val_if_fail (E_IS_WORDPRESS_TRACKING_MANAGER (self), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);
	content_type = camel_mime_part_get_content_type (CAMEL_MIME_PART (message));
	if (track_openings && content_type &&
	    (camel_content_type_is (content_type, "multipart", "signed") ||
	     camel_content_type_is (content_type, "multipart", "encrypted") ||
	     camel_content_type_is (content_type, "application", "pkcs7-mime") ||
	     camel_content_type_is (content_type, "application", "x-pkcs7-mime"))) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Tracking cannot be combined with a digitally signed or encrypted message. Turn off tracking to preserve message security."));
		return FALSE;
	}
	if (track_openings && (!self->registered_external_id || !self->registered_tracking_url ||
	    !self->registered_project_id)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("WordPress did not register a tracking pixel for this message."));
		return FALSE;
	}
	if (track_openings) {
		configured_site = e_wordpress_tracking_manager_dup_site_url (self);
		configured_project = e_wordpress_tracking_manager_dup_project_id (self);
		if (!tracking_url_matches (configured_site, self->registered_tracking_url,
			self->registered_project_id, error) ||
		    g_strcmp0 (configured_project, self->registered_project_id) != 0) {
			if (!error || !*error)
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
					_("The WordPress site or project changed after pixel registration. Review the settings and retry."));
			g_free (configured_site);
			g_free (configured_project);
			return FALSE;
		}
		g_free (configured_site);
		g_free (configured_project);
	}
	if (!tracking_prepare_part (self, CAMEL_MIME_PART (message), track_openings,
		&found_html, &inserted_pixel, error))
		return FALSE;
	if (track_openings && !found_html) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Tracking pixels require an HTML message. Switch to HTML or turn off tracking."));
		return FALSE;
	}
	if (track_openings && !inserted_pixel) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("The tracking pixel could not be added to the HTML message."));
		return FALSE;
	}
	if (track_openings) {
		header = g_strdup_printf ("v=1; project=%s; external=%s",
			self->registered_project_id, self->registered_external_id);
		camel_medium_set_header (CAMEL_MEDIUM (message), "X-MiPipe-Tracking", header);
	} else {
		camel_medium_remove_header (CAMEL_MEDIUM (message), "X-MiPipe-Tracking");
	}
	g_free (header);
	return TRUE;
}
