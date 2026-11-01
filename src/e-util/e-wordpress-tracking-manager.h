#ifndef E_WORDPRESS_TRACKING_MANAGER_H
#define E_WORDPRESS_TRACKING_MANAGER_H

#include <gio/gio.h>
#include <camel/camel.h>

G_BEGIN_DECLS

#define E_TYPE_WORDPRESS_TRACKING_MANAGER (e_wordpress_tracking_manager_get_type ())
G_DECLARE_FINAL_TYPE (EWordPressTrackingManager, e_wordpress_tracking_manager, E, WORDPRESS_TRACKING_MANAGER, GObject)

typedef struct _EWordPressTrackingSummary EWordPressTrackingSummary;

EWordPressTrackingManager *e_wordpress_tracking_manager_new (void);
gboolean e_wordpress_tracking_manager_prepare_message (EWordPressTrackingManager *self,
                                                       CamelMimeMessage *message,
                                                       gboolean track_openings,
                                                       GError **error);

gboolean e_wordpress_tracking_manager_get_enabled (EWordPressTrackingManager *self);
void e_wordpress_tracking_manager_set_enabled (EWordPressTrackingManager *self,
                                               gboolean enabled);
gboolean e_wordpress_tracking_manager_get_default (EWordPressTrackingManager *self);
void e_wordpress_tracking_manager_set_default (EWordPressTrackingManager *self,
                                                gboolean enabled);
gchar *e_wordpress_tracking_manager_dup_site_url (EWordPressTrackingManager *self);
void e_wordpress_tracking_manager_set_site_url (EWordPressTrackingManager *self,
                                                 const gchar *site_url);
gchar *e_wordpress_tracking_manager_dup_project_id (EWordPressTrackingManager *self);
void e_wordpress_tracking_manager_set_project_id (EWordPressTrackingManager *self,
                                                   const gchar *project_id);
gboolean e_wordpress_tracking_manager_validate_configuration (const gchar *site_url,
                                                               const gchar *project_id,
                                                               GError **error);
gboolean e_wordpress_tracking_manager_has_key (EWordPressTrackingManager *self,
                                                GError **error);
gboolean e_wordpress_tracking_manager_store_key (EWordPressTrackingManager *self,
                                                  const gchar *key,
                                                  GError **error);
gboolean e_wordpress_tracking_manager_remove_key (EWordPressTrackingManager *self,
                                                   GError **error);
gboolean e_wordpress_tracking_manager_is_configured (EWordPressTrackingManager *self,
                                                      GError **error);

gchar *e_wordpress_tracking_manager_generate_external_id (void);
void e_wordpress_tracking_manager_register_async (EWordPressTrackingManager *self,
                                                   const gchar *external_id,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
gchar *e_wordpress_tracking_manager_register_finish (EWordPressTrackingManager *self,
                                                      GAsyncResult *result,
                                                      GError **error);
void e_wordpress_tracking_manager_test_async (EWordPressTrackingManager *self,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
gboolean e_wordpress_tracking_manager_test_finish (EWordPressTrackingManager *self,
                                                    GAsyncResult *result,
                                                    GError **error);
void e_wordpress_tracking_manager_summary_async (EWordPressTrackingManager *self,
                                                  const gchar *external_id,
                                                  GCancellable *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
EWordPressTrackingSummary *e_wordpress_tracking_manager_summary_finish (EWordPressTrackingManager *self,
                                                                          GAsyncResult *result,
                                                                          GError **error);
guint64 e_wordpress_tracking_summary_get_count (const EWordPressTrackingSummary *summary);
const gchar *e_wordpress_tracking_summary_get_first_request (const EWordPressTrackingSummary *summary);
const gchar *e_wordpress_tracking_summary_get_latest_request (const EWordPressTrackingSummary *summary);
void e_wordpress_tracking_summary_free (EWordPressTrackingSummary *summary);

G_END_DECLS

#endif
