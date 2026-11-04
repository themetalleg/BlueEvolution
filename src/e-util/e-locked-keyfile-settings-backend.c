#include <gio/gio.h>
#include <glib/gstdio.h>

#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

typedef struct {
	GSettingsBackend parent;
	GMutex mutex;
	GSettingsBackend *delegate;
	GKeyFile *snapshot;
	GFileMonitor *monitor;
	gchar *filename;
	gchar *lockname;
} ELockedKeyfileSettingsBackend;

typedef GSettingsBackendClass ELockedKeyfileSettingsBackendClass;

GType e_locked_keyfile_settings_backend_get_type (void);
void g_io_module_load (GIOModule *module);
void g_io_module_unload (GIOModule *module);
gchar **g_io_module_query (void);

G_DEFINE_TYPE (ELockedKeyfileSettingsBackend, e_locked_keyfile_settings_backend, G_TYPE_SETTINGS_BACKEND)

static void
locked_keyfile_reload (ELockedKeyfileSettingsBackend *self)
{
	g_clear_object (&self->delegate);
	self->delegate = g_keyfile_settings_backend_new (self->filename, "/", NULL);
}

static GVariant *
locked_keyfile_read (GSettingsBackend *backend,
		    const gchar *key,
		    const GVariantType *expected_type,
		    gboolean default_value)
{
	ELockedKeyfileSettingsBackend *self = (ELockedKeyfileSettingsBackend *) backend;
	GVariant *value;

	g_mutex_lock (&self->mutex);
	value = G_SETTINGS_BACKEND_GET_CLASS (self->delegate)->read (
		self->delegate, key, expected_type, default_value);
	g_mutex_unlock (&self->mutex);

	return value;
}

static gboolean
locked_keyfile_get_writable (GSettingsBackend *backend,
			    const gchar *key)
{
	ELockedKeyfileSettingsBackend *self = (ELockedKeyfileSettingsBackend *) backend;
	gboolean writable;

	g_mutex_lock (&self->mutex);
	writable = G_SETTINGS_BACKEND_GET_CLASS (self->delegate)->get_writable (self->delegate, key);
	g_mutex_unlock (&self->mutex);

	return writable;
}

static gint
locked_keyfile_compare (gconstpointer left,
		       gconstpointer right)
{
	return g_strcmp0 (left, right);
}

static GKeyFile *
locked_keyfile_load_file (ELockedKeyfileSettingsBackend *self)
{
	GKeyFile *keyfile = g_key_file_new ();
	GError *error = NULL;

	if (!g_key_file_load_from_file (keyfile, self->filename, G_KEY_FILE_NONE, &error) &&
	    !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_warning ("Cannot load settings keyfile; refusing to overwrite it (error %s:%d)",
			g_quark_to_string (error->domain), error->code);
		g_key_file_unref (keyfile);
		keyfile = NULL;
	}
	g_clear_error (&error);
	return keyfile;
}

static gint
locked_keyfile_lock (ELockedKeyfileSettingsBackend *self)
{
	GKeyFile *keyfile;
	gint fd, result;

	fd = g_open (self->lockname, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (fd == -1) {
		g_warning ("Cannot open settings lock: %s", g_strerror (errno));
		return -1;
	}

	do {
		result = flock (fd, LOCK_EX);
	} while (result == -1 && errno == EINTR);

	if (result == -1) {
		g_warning ("Cannot lock settings: %s", g_strerror (errno));
		close (fd);
		return -1;
	}

	/* GLib otherwise treats an unreadable or malformed keyfile as empty. */
	keyfile = locked_keyfile_load_file (self);
	if (!keyfile) {
		close (fd);
		return -1;
	}
	g_key_file_unref (keyfile);

	/* Reload under the inter-process lock, not after an asynchronous file notification. */
	locked_keyfile_reload (self);
	return fd;
}

static gboolean
locked_keyfile_write (GSettingsBackend *backend,
		     const gchar *key,
		     GVariant *value,
		     gpointer origin_tag)
{
	ELockedKeyfileSettingsBackend *self = (ELockedKeyfileSettingsBackend *) backend;
	gboolean success = FALSE;
	gint fd;

	g_mutex_lock (&self->mutex);
	fd = locked_keyfile_lock (self);
	if (fd != -1) {
		success = G_SETTINGS_BACKEND_GET_CLASS (self->delegate)->write (
			self->delegate, key, value, origin_tag);
		close (fd);
		if (!success)
			locked_keyfile_reload (self);
	}
	g_mutex_unlock (&self->mutex);

	if (success)
		g_settings_backend_changed (backend, key, origin_tag);
	else
		g_warning ("Failed to save application setting");

	return success;
}

static gboolean
locked_keyfile_write_tree (GSettingsBackend *backend,
			  GTree *tree,
			  gpointer origin_tag)
{
	ELockedKeyfileSettingsBackend *self = (ELockedKeyfileSettingsBackend *) backend;
	gboolean success = FALSE;
	gint fd;

	g_mutex_lock (&self->mutex);
	fd = locked_keyfile_lock (self);
	if (fd != -1) {
		success = G_SETTINGS_BACKEND_GET_CLASS (self->delegate)->write_tree (
			self->delegate, tree, origin_tag);
		close (fd);
		if (!success)
			locked_keyfile_reload (self);
	}
	g_mutex_unlock (&self->mutex);

	if (success)
		g_settings_backend_changed_tree (backend, tree, origin_tag);
	else
		g_warning ("Failed to save application settings");

	return success;
}

static void
locked_keyfile_reset (GSettingsBackend *backend,
		     const gchar *key,
		     gpointer origin_tag)
{
	GTree *tree = g_tree_new (locked_keyfile_compare);

	g_tree_insert (tree, (gpointer) key, NULL);
	locked_keyfile_write_tree (backend, tree, origin_tag);
	g_tree_unref (tree);
}

static void
locked_keyfile_find_changes (GKeyFile *before,
			    GKeyFile *after,
			    GTree *changes)
{
	gchar **groups = g_key_file_get_groups (before, NULL);
	guint ii, jj;

	for (ii = 0; groups[ii]; ii++) {
		gchar **keys = g_key_file_get_keys (before, groups[ii], NULL, NULL);

		for (jj = 0; keys[jj]; jj++) {
			gchar *old_value = g_key_file_get_value (before, groups[ii], keys[jj], NULL);
			gchar *new_value = g_key_file_get_value (after, groups[ii], keys[jj], NULL);

			if (g_strcmp0 (old_value, new_value) != 0)
				g_tree_insert (changes, g_strconcat ("/", groups[ii], "/", keys[jj], NULL), NULL);
			g_free (old_value);
			g_free (new_value);
		}
		g_strfreev (keys);
	}
	g_strfreev (groups);
}

static gint
locked_keyfile_compare_data (gconstpointer left,
			    gconstpointer right,
			    gpointer user_data)
{
	return locked_keyfile_compare (left, right);
}

static void
locked_keyfile_changed (GFileMonitor *monitor,
		       GFile *file,
		       GFile *other_file,
		       GFileMonitorEvent event,
		       gpointer user_data)
{
	ELockedKeyfileSettingsBackend *self = user_data;
	gchar *basename = g_file_get_basename (file);
	gboolean changed = g_strcmp0 (basename, "keyfile") == 0;
	GKeyFile *snapshot;
	GTree *changes;

	g_free (basename);
	if (!changed)
		return;

	changes = g_tree_new_full (locked_keyfile_compare_data, NULL, g_free, NULL);
	g_mutex_lock (&self->mutex);
	snapshot = locked_keyfile_load_file (self);
	if (snapshot) {
		locked_keyfile_find_changes (self->snapshot, snapshot, changes);
		locked_keyfile_find_changes (snapshot, self->snapshot, changes);
		g_key_file_unref (self->snapshot);
		self->snapshot = snapshot;
		locked_keyfile_reload (self);
	}
	g_mutex_unlock (&self->mutex);
	if (g_tree_nnodes (changes))
		g_settings_backend_changed_tree (G_SETTINGS_BACKEND (self), changes, NULL);
	g_tree_unref (changes);
}

static void
locked_keyfile_finalize (GObject *object)
{
	ELockedKeyfileSettingsBackend *self = (ELockedKeyfileSettingsBackend *) object;

	if (self->monitor) {
		g_signal_handlers_disconnect_by_data (self->monitor, self);
		g_file_monitor_cancel (self->monitor);
	}
	g_clear_object (&self->monitor);
	g_clear_object (&self->delegate);
	g_key_file_unref (self->snapshot);
	g_free (self->filename);
	g_free (self->lockname);
	g_mutex_clear (&self->mutex);

	G_OBJECT_CLASS (e_locked_keyfile_settings_backend_parent_class)->finalize (object);
}

static void
e_locked_keyfile_settings_backend_class_init (ELockedKeyfileSettingsBackendClass *class)
{
	GSettingsBackendClass *backend_class = G_SETTINGS_BACKEND_CLASS (class);

	backend_class->read = locked_keyfile_read;
	backend_class->get_writable = locked_keyfile_get_writable;
	backend_class->write = locked_keyfile_write;
	backend_class->write_tree = locked_keyfile_write_tree;
	backend_class->reset = locked_keyfile_reset;
	G_OBJECT_CLASS (class)->finalize = locked_keyfile_finalize;
}

static void
e_locked_keyfile_settings_backend_init (ELockedKeyfileSettingsBackend *self)
{
	gchar *directory = g_build_filename (g_get_user_config_dir (), "glib-2.0", "settings", NULL);
	GFile *file;
	GError *error = NULL;

	g_mutex_init (&self->mutex);
	self->filename = g_build_filename (directory, "keyfile", NULL);
	self->lockname = g_build_filename (directory, "keyfile.lock", NULL);
	if (g_mkdir_with_parents (directory, 0700) == -1)
		g_warning ("Cannot create settings directory: %s", g_strerror (errno));
	self->snapshot = locked_keyfile_load_file (self);
	if (!self->snapshot)
		self->snapshot = g_key_file_new ();
	locked_keyfile_reload (self);
	file = g_file_new_for_path (directory);
	self->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, &error);
	if (self->monitor)
		g_signal_connect (self->monitor, "changed", G_CALLBACK (locked_keyfile_changed), self);
	else
		g_warning ("Cannot monitor settings directory: %s", error->message);
	g_clear_error (&error);
	g_object_unref (file);
	g_free (directory);
}

void
g_io_module_load (GIOModule *module)
{
	g_type_module_use (G_TYPE_MODULE (module));
	g_io_extension_point_implement (G_SETTINGS_BACKEND_EXTENSION_POINT_NAME,
		e_locked_keyfile_settings_backend_get_type (), "blueevolution-keyfile", -10);
}

void
g_io_module_unload (GIOModule *module)
{
}

gchar **
g_io_module_query (void)
{
	return g_strsplit (G_SETTINGS_BACKEND_EXTENSION_POINT_NAME, "!", 0);
}
