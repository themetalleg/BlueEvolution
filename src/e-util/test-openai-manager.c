#include "e-openai-manager.c"

static void
test_response (void)
{
	const gchar *json = "{\"status\":\"completed\",\"output\":[{\"type\":\"reasoning\"},{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Hello\"},{\"type\":\"output_text\",\"text\":\" there\"}]}]}";
	GError *error = NULL;
	gchar *text = openai_parse_response (json, strlen (json), &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, "Hello there");
	g_free (text);
}

static void
test_invalid_response (void)
{
	const gchar *cases[] = { "not JSON", "[]", "{}", "{\"status\":\"incomplete\"}",
		"{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"refusal\",\"refusal\":\"no\"}]}]}" };
	guint index;
	for (index = 0; index < G_N_ELEMENTS (cases); index++) {
		GError *error = NULL;
		g_assert_null (openai_parse_response (cases[index], strlen (cases[index]), &error));
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
		g_clear_error (&error);
	}
}

static void
test_privacy_body (void)
{
	OpenAIJob job = { .kind = E_OPENAI_SUGGEST_REPLIES, .language = "de", .email = "Synthetic email", .prompt = "Synthetic request" };
	gchar *body = openai_build_body (&job);
	JsonNode *root = json_from_string (body, NULL);
	JsonObject *object = json_node_get_object (root);
	g_assert_false (json_object_get_boolean_member (object, "store"));
	g_assert_false (json_object_has_member (object, "tools"));
	g_assert_nonnull (strstr (openai_string (object, "instructions"), "untrusted"));
	g_assert_nonnull (strstr (openai_string (object, "instructions"), "UI language (de)"));
	g_assert_nonnull (strstr (openai_string (object, "input"), "Synthetic email"));
	object = json_object_get_object_member (json_object_get_object_member (object, "text"), "format");
	g_assert_cmpstr (openai_string (object, "type"), ==, "json_schema");
	g_assert_true (json_object_get_boolean_member (object, "strict"));
	json_node_free (root);
	g_free (body);
}

static void
consent_finished (GObject *source, GAsyncResult *result, gpointer data)
{
	GError *error = NULL;
	g_assert_false (e_openai_manager_set_key_finish (E_OPENAI_MANAGER (source), result, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
	g_clear_error (&error);
	g_main_loop_quit (data);
}

static void
test_consent (void)
{
	EOpenAIManager *manager = e_openai_manager_ref_default ();
	GMainLoop *loop = g_main_loop_new (NULL, FALSE);
	e_openai_manager_set_key (manager, "synthetic-not-a-real-key", FALSE, NULL, consent_finished, loop);
	g_main_loop_run (loop);
	g_main_loop_unref (loop);
	g_object_unref (manager);
}

typedef struct {
	GMainLoop *loop;
	OpenAIJob job;
	gchar *api, *result;
	GError *error;
	guint status, requests;
	gboolean cancel;
} MockRequest;

static void
mock_handler (SoupServer *server, SoupServerMessage *message, const gchar *path,
              GHashTable *query, gpointer data)
{
	MockRequest *request = data;
	const gchar *body;
	request->requests++;
	if (request->job.provider == 0)
		g_assert_cmpstr (soup_message_headers_get_one (soup_server_message_get_request_headers (message), "Authorization"),
			==, "Bearer synthetic-test-token");
	else {
		g_assert_null (soup_message_headers_get_one (soup_server_message_get_request_headers (message), "Authorization"));
		g_assert_cmpstr (soup_message_headers_get_one (soup_server_message_get_request_headers (message),
			request->job.provider == 1 ? "x-goog-api-key" : "x-api-key"), ==, "synthetic-test-token");
		g_assert_null (query);
	}
	{
		SoupMessageBody *input = soup_server_message_get_request_body (message);
		gchar *json = g_strndup (input->data, input->length);
		JsonNode *root = json_from_string (json, NULL);
		g_assert_nonnull (root);
		g_assert_false (json_object_has_member (json_node_get_object (root), "tools"));
		if (request->job.provider == 0)
			g_assert_false (json_object_get_boolean_member (json_node_get_object (root), "store"));
		else if (request->job.provider == 1)
			g_assert_nonnull (assistant_array (json_node_get_object (root), "contents"));
		else {
			g_assert_nonnull (assistant_array (json_node_get_object (root), "messages"));
			g_assert_cmpstr (soup_message_headers_get_one (soup_server_message_get_request_headers (message), "anthropic-version"), ==, "2023-06-01");
		}
		if (request->job.kind == E_OPENAI_TEST_KEY && request->job.provider == 0) {
			g_assert_cmpstr (openai_string (json_node_get_object (root), "input"), ==, "Reply with OK.");
			g_assert_cmpint (json_object_get_int_member (json_node_get_object (root), "max_output_tokens"), ==, 16);
			g_assert_null (strstr (json, "Synthetic message"));
		}
		if (request->job.kind == E_OPENAI_TEST_KEY)
			g_assert_null (strstr (json, "Synthetic message"));
		json_node_free (root);
		g_free (json);
		g_assert_cmpstr (path, ==, request->job.provider == 1 ? "/gemini-2.5-flash:generateContent" : request->job.provider == 2 ? "/messages" : "/responses");
	}
	if (request->status == 307)
		soup_message_headers_replace (soup_server_message_get_response_headers (message), "Location", "/redirected");
	body = request->status == 200 ?
		"{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Synthetic reply\"}]}]}" :
		"{\"error\":{\"message\":\"Do not expose synthetic-test-token\"}}";
	if (request->status == 200 && request->job.provider == 1)
		body = "{\"candidates\":[{\"finishReason\":\"STOP\",\"content\":{\"parts\":[{\"text\":\"Synthetic reply\"}]}}]}";
	if (request->status == 200 && request->job.provider == 2)
		body = "{\"stop_reason\":\"end_turn\",\"content\":[{\"type\":\"text\",\"text\":\"Synthetic reply\"}]}";
	if (request->status == 429)
		body = request->job.kind == E_OPENAI_TEST_KEY ?
			"{\"error\":{\"code\":\"insufficient_quota\",\"message\":\"synthetic-test-token\"}}" :
			"{\"error\":{\"code\":\"rate_limit_exceeded\",\"message\":\"synthetic-test-token\"}}";
	soup_server_message_set_status (message, request->status, NULL);
	soup_server_message_set_response (message, "application/json", SOUP_MEMORY_COPY, body, strlen (body));
}

static void
mock_thread (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
	MockRequest *request = data;
	GError *error = NULL;
	gchar *text = openai_send (&request->job, "synthetic-test-token", request->api, cancellable, &error);
	if (error)
		g_task_return_error (task, error);
	else
		g_task_return_pointer (task, text, g_free);
}

static void
mock_finished (GObject *source, GAsyncResult *result, gpointer data)
{
	MockRequest *request = data;
	request->result = g_task_propagate_pointer (G_TASK (result), &request->error);
	g_main_loop_quit (request->loop);
}

static void
test_http (gconstpointer data)
{
	guint scenario = GPOINTER_TO_UINT (data);
	MockRequest request = { 0 };
	SoupServer *server = soup_server_new (NULL, NULL);
	GCancellable *cancellable = g_cancellable_new ();
	GTask *task;
	GSList *uris;
	GError *error = NULL;
	request.job.provider = scenario >> 8;
	scenario &= 255;
	request.status = scenario == 1 || scenario == 2 || scenario == 6 ? 200 : scenario == 3 ? 401 : scenario == 4 || scenario == 7 ? 429 : 307;
	request.job.kind = scenario == 1 || scenario == 7 ? E_OPENAI_TEST_KEY : E_OPENAI_DRAFT_REPLY;
	request.job.email = "Synthetic message";
	request.job.prompt = "Accept politely";
	request.loop = g_main_loop_new (NULL, FALSE);
	soup_server_add_handler (server, NULL, mock_handler, &request, NULL);
	g_assert_true (soup_server_listen_local (server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
	g_assert_no_error (error);
	uris = soup_server_get_uris (server);
	request.api = g_uri_to_string (uris->data);
	g_slist_free_full (uris, (GDestroyNotify) g_uri_unref);
	if (scenario == 6)
		g_cancellable_cancel (cancellable);
	task = g_task_new (NULL, cancellable, mock_finished, &request);
	g_task_set_task_data (task, &request, NULL);
	g_task_run_in_thread (task, mock_thread);
	g_object_unref (task);
	g_main_loop_run (request.loop);
	if (scenario == 1 || scenario == 2) {
		g_assert_no_error (request.error);
		g_assert_cmpstr (request.result, ==, scenario == 1 ? "valid" : "Synthetic reply");
	} else {
		g_assert_nonnull (request.error);
		g_assert_null (request.result);
		g_assert_null (strstr (request.error->message, "synthetic-test-token"));
		if (scenario == 3)
			g_assert_error (request.error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
		if (scenario == 6)
			g_assert_error (request.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
		if (scenario == 4 && request.job.provider == 0)
			g_assert_nonnull (strstr (request.error->message, "rate limit"));
		if (scenario == 7 && request.job.provider == 0)
			g_assert_nonnull (strstr (request.error->message, "quota"));
	}
	g_assert_cmpuint (request.requests, ==, scenario == 6 ? 0 : 1);
	g_clear_error (&request.error);
	g_free (request.result);
	g_free (request.api);
	g_main_loop_unref (request.loop);
	soup_server_disconnect (server);
	g_object_unref (server);
	g_object_unref (cancellable);
}

static void
test_providers (void)
{
	guint index;
	for (index = 0; index < G_N_ELEMENTS (provider_ids); index++) {
		EOpenAIManager *manager = e_openai_manager_new_for_provider (provider_ids[index]);
		gchar *model = e_openai_manager_dup_model (manager);
		g_assert_cmpstr (model, ==, provider_models[index]);
		g_assert_cmpstr (e_openai_manager_get_provider_name (manager), ==, provider_names[index]);
		g_assert_false (e_openai_manager_set_model (manager, "bad/model?key=secret"));
		g_assert_true (e_openai_manager_set_model (manager, "synthetic-model"));
		g_free (model);
		model = e_openai_manager_dup_model (manager);
		g_assert_cmpstr (model, ==, "synthetic-model");
		g_assert_true (e_openai_manager_set_model (manager, provider_models[index]));
		g_assert_cmpstr (provider_schemas[index]->name, !=, provider_schemas[(index + 1) % 3]->name);
		g_free (model);
		g_object_unref (manager);
	}
	g_assert_null (e_openai_manager_new_for_provider ("unsupported"));
}

static void
test_provider_responses (void)
{
	const gchar *invalid[] = { "[]", "{}", "invalid", "{\"content\":1}",
		"{\"stop_reason\":\"max_tokens\",\"content\":[{\"type\":\"text\",\"text\":\"truncated\"}]}",
		"{\"candidates\":[{\"finishReason\":\"SAFETY\",\"content\":{\"parts\":[{\"text\":\"blocked\"}]}}]}" };
	guint provider, index;
	for (provider = 1; provider < 3; provider++) {
		for (index = 0; index < G_N_ELEMENTS (invalid); index++) {
			GError *error = NULL;
			g_assert_null (assistant_parse_response (provider, invalid[index], strlen (invalid[index]), &error));
			g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
			g_clear_error (&error);
		}
	}
}

int main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/openai/response", test_response);
	g_test_add_func ("/openai/invalid-response", test_invalid_response);
	g_test_add_func ("/openai/privacy-body", test_privacy_body);
	g_test_add_func ("/openai/consent-required", test_consent);
	g_test_add_func ("/assistant/providers", test_providers);
	g_test_add_func ("/assistant/invalid-responses", test_provider_responses);
	g_test_add_data_func ("/assistant/gemini/key-test", GUINT_TO_POINTER (257), test_http);
	g_test_add_data_func ("/assistant/gemini/generate", GUINT_TO_POINTER (258), test_http);
	g_test_add_data_func ("/assistant/gemini/unauthorized", GUINT_TO_POINTER (259), test_http);
	g_test_add_data_func ("/assistant/gemini/no-redirect", GUINT_TO_POINTER (261), test_http);
	g_test_add_data_func ("/assistant/claude/key-test", GUINT_TO_POINTER (513), test_http);
	g_test_add_data_func ("/assistant/claude/generate", GUINT_TO_POINTER (514), test_http);
	g_test_add_data_func ("/assistant/claude/unauthorized", GUINT_TO_POINTER (515), test_http);
	g_test_add_data_func ("/assistant/claude/no-redirect", GUINT_TO_POINTER (517), test_http);
	g_test_add_data_func ("/openai/http/key-test", GUINT_TO_POINTER (1), test_http);
	g_test_add_data_func ("/openai/http/generate", GUINT_TO_POINTER (2), test_http);
	g_test_add_data_func ("/openai/http/unauthorized", GUINT_TO_POINTER (3), test_http);
	g_test_add_data_func ("/openai/http/rate-limit", GUINT_TO_POINTER (4), test_http);
	g_test_add_data_func ("/openai/http/no-redirect", GUINT_TO_POINTER (5), test_http);
	g_test_add_data_func ("/openai/http/cancelled", GUINT_TO_POINTER (6), test_http);
	g_test_add_data_func ("/openai/http/key-test-quota-exhausted", GUINT_TO_POINTER (7), test_http);
	return g_test_run ();
}