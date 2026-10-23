#ifndef E_OPENAI_MANAGER_H
#define E_OPENAI_MANAGER_H
#include <gio/gio.h>
G_BEGIN_DECLS
#define E_TYPE_OPENAI_MANAGER (e_openai_manager_get_type ())
G_DECLARE_FINAL_TYPE (EOpenAIManager, e_openai_manager, E, OPENAI_MANAGER, GObject)
typedef enum { E_OPENAI_HAS_KEY, E_OPENAI_TEST_KEY, E_OPENAI_SUGGEST_REPLIES, E_OPENAI_DRAFT_REPLY } EOpenAIRequest;
EOpenAIManager *e_openai_manager_ref_default (void);
EOpenAIManager *e_openai_manager_new_for_provider (const gchar *provider);
const gchar *e_openai_manager_get_provider (EOpenAIManager *self);
const gchar *e_openai_manager_get_provider_name (EOpenAIManager *self);
gchar *e_openai_manager_dup_model (EOpenAIManager *self);
gboolean e_openai_manager_set_model (EOpenAIManager *self, const gchar *model);
void e_openai_manager_set_key (EOpenAIManager *self, const gchar *key, gboolean consent,
	GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean e_openai_manager_set_key_finish (EOpenAIManager *self, GAsyncResult *result, GError **error);
void e_openai_manager_request (EOpenAIManager *self, EOpenAIRequest kind,
	const gchar *email, const gchar *prompt, const gchar *history,
	GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gchar *e_openai_manager_request_finish (EOpenAIManager *self, GAsyncResult *result, GError **error);
G_END_DECLS
#endif