#include <gio/gio.h>
#include <glib/gstdio.h>

#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>

#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#define TRACKING_SCHEMA "io.github.themetalleg.BlueEvolution"
#define MAIL_SCHEMA "org.gnome.evolution.mail"
#define TRACKING_GROUP "io/github/themetalleg/BlueEvolution"
#define MAIL_GROUP "org/gnome/evolution/mail"

typedef struct {
	GPid pid;
	gint input;
} Writer;

static gchar *executable;
static gchar *filename;

static Writer
start_writer (const gchar *operation,
	      const gchar *schema,
	      const gchar *key,
	      const gchar *value)
{
	gchar *args[] = { executable, (gchar *) operation, (gchar *) schema,
		(gchar *) key, (gchar *) value, NULL };
	Writer writer;
	gint output;
	gchar ready;
	GError *error = NULL;

	g_assert_true (g_spawn_async_with_pipes (NULL, args, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
		NULL, NULL, &writer.pid, &writer.input, &output, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpint (read (output, &ready, 1), ==, 1);
	g_assert_cmpint (ready, ==, 'R');
	close (output);
	return writer;
}

static void
release_writer (Writer *writer)
{
	g_assert_cmpint (write (writer->input, "W", 1), ==, 1);
	close (writer->input);
}

static void
wait_writer (Writer *writer,
	     gint expected_status)
{
	gint status, result;

	do {
		result = waitpid (writer->pid, &status, 0);
	} while (result == -1 && errno == EINTR);
	g_assert_cmpint (result, ==, writer->pid);
	g_assert_true (WIFEXITED (status));
	g_assert_cmpint (WEXITSTATUS (status), ==, expected_status);
	g_spawn_close_pid (writer->pid);
}

static void
assert_saved (const gchar *group,
	      const gchar *key,
	      const gchar *expected)
{
	GKeyFile *keyfile = g_key_file_new ();
	GError *error = NULL;
	gchar *value;

	g_assert_true (g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error));
	g_assert_no_error (error);
	value = g_key_file_get_value (keyfile, group, key, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (value, ==, expected);
	g_free (value);
	g_key_file_unref (keyfile);
}

static void
test_existing_profile (void)
{
	GSettings *settings = g_settings_new (TRACKING_SCHEMA);
	gchar *site = g_settings_get_string (settings, "mail-tracking-site-url");

	g_assert_cmpstr (G_OBJECT_TYPE_NAME (g_settings_backend_get_default ()), ==,
		"ELockedKeyfileSettingsBackend");
	g_assert_true (g_settings_get_boolean (settings, "mail-tracking-enabled"));
	g_assert_cmpstr (site, ==, "https://tracking.example.test");
	g_assert_true (g_settings_set_string (settings, "appearance-mode", "dark"));
	assert_saved (MAIL_GROUP, "layout", "0");
	assert_saved ("unknown/group", "keep", "'unchanged'");
	g_free (site);
	g_object_unref (settings);
}

static void
test_stale_writer (void)
{
	Writer writer = start_writer ("--write", MAIL_SCHEMA, "paned-size", "456");
	GSettings *tracking = g_settings_new (TRACKING_SCHEMA);
	GSettings *mail = g_settings_new (MAIL_SCHEMA);

	g_assert_true (g_settings_set_string (tracking, "mail-tracking-project-id", "Ab3dE7fG9h"));
	g_assert_true (g_settings_set_boolean (tracking, "mail-tracking-enabled", TRUE));
	g_assert_true (g_settings_set_boolean (tracking, "mail-tracking-default", TRUE));
	g_assert_true (g_settings_set_int (mail, "layout", 0));
	release_writer (&writer);
	wait_writer (&writer, 0);

	assert_saved (TRACKING_GROUP, "mail-tracking-project-id", "'Ab3dE7fG9h'");
	assert_saved (TRACKING_GROUP, "mail-tracking-enabled", "true");
	assert_saved (TRACKING_GROUP, "mail-tracking-default", "true");
	assert_saved (MAIL_GROUP, "layout", "0");
	assert_saved (MAIL_GROUP, "paned-size", "456");
	assert_saved ("unknown/group", "keep", "'unchanged'");
	g_object_unref (tracking);
	g_object_unref (mail);
}

static void
test_simultaneous_writers (void)
{
	const gchar *keys[] = { "paned-size", "paned-size-sub", "hpaned-size", "hpaned-size-sub" };
	Writer writers[G_N_ELEMENTS (keys)];
	guint round, ii;

	for (round = 0; round < 10; round++) {
		gchar *value = g_strdup_printf ("%u", 600 + round);

		for (ii = 0; ii < G_N_ELEMENTS (keys); ii++)
			writers[ii] = start_writer ("--write", MAIL_SCHEMA, keys[ii], value);
		for (ii = 0; ii < G_N_ELEMENTS (keys); ii++)
			release_writer (&writers[ii]);
		for (ii = 0; ii < G_N_ELEMENTS (keys); ii++) {
			wait_writer (&writers[ii], 0);
			assert_saved (MAIL_GROUP, keys[ii], value);
		}
		g_free (value);
	}
	assert_saved (TRACKING_GROUP, "mail-tracking-default", "true");
}

static void
test_delayed_write (void)
{
	GSettings *mail = g_settings_new (MAIL_SCHEMA);
	Writer writer = start_writer ("--write", TRACKING_SCHEMA, "mail-tracking-site-url",
		"'https://updated.example.test'");

	g_settings_delay (mail);
	g_assert_true (g_settings_set_int (mail, "layout", 0));
	g_assert_true (g_settings_set_int (mail, "paned-size", 765));
	release_writer (&writer);
	wait_writer (&writer, 0);
	g_settings_apply (mail);

	assert_saved (TRACKING_GROUP, "mail-tracking-site-url", "'https://updated.example.test'");
	assert_saved (MAIL_GROUP, "layout", "0");
	assert_saved (MAIL_GROUP, "paned-size", "765");
	g_object_unref (mail);
}

static void
test_reset (void)
{
	Writer writer = start_writer ("--reset", MAIL_SCHEMA, "paned-size", "0");
	GSettings *settings = g_settings_new (TRACKING_SCHEMA);
	GKeyFile *keyfile = g_key_file_new ();

	g_assert_true (g_settings_set_string (settings, "appearance-mode", "light"));
	release_writer (&writer);
	wait_writer (&writer, 0);
	g_assert_true (g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, NULL));
	g_assert_false (g_key_file_has_key (keyfile, MAIL_GROUP, "paned-size", NULL));
	assert_saved (TRACKING_GROUP, "appearance-mode", "'light'");
	assert_saved (MAIL_GROUP, "layout", "0");
	g_key_file_unref (keyfile);
	g_object_unref (settings);
}

static void
setting_changed (GSettings *settings,
		 const gchar *key,
		 gpointer user_data)
{
	gboolean *notified = user_data;

	if (g_settings_get_int (settings, key) == 987)
		*notified = TRUE;
}

static void
unrelated_setting_changed (GSettings *settings,
			   const gchar *key,
			   gpointer user_data)
{
	guint *notifications = user_data;

	(*notifications)++;
}

static void
test_notification (void)
{
	GSettings *settings = g_settings_new (MAIL_SCHEMA);
	gboolean notified = FALSE;
	guint unrelated_notifications = 0;
	gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
	Writer writer = start_writer ("--write", MAIL_SCHEMA, "paned-size", "987");

	while (g_main_context_iteration (NULL, FALSE))
		;
	g_signal_connect (settings, "changed::paned-size", G_CALLBACK (setting_changed), &notified);
	g_signal_connect (settings, "changed::layout", G_CALLBACK (unrelated_setting_changed), &unrelated_notifications);
	release_writer (&writer);
	wait_writer (&writer, 0);
	while (!notified && g_get_monotonic_time () < deadline) {
		while (g_main_context_iteration (NULL, FALSE))
			;
		g_usleep (1000);
	}
	g_assert_true (notified);
	g_assert_cmpuint (unrelated_notifications, ==, 0);
	g_assert_cmpint (g_settings_get_int (settings, "paned-size"), ==, 987);
	g_object_unref (settings);
}

static void
test_restart (void)
{
	GSettings *tracking = g_settings_new (TRACKING_SCHEMA);
	GSettings *mail = g_settings_new (MAIL_SCHEMA);
	Writer writer;

	g_assert_true (g_settings_set_string (tracking, "mail-tracking-site-url",
		"https://restart.example.test"));
	g_assert_true (g_settings_set_int (mail, "layout", 0));
	g_object_unref (tracking);
	g_object_unref (mail);

	writer = start_writer ("--check", TRACKING_SCHEMA, "mail-tracking-site-url",
		"'https://restart.example.test'");
	release_writer (&writer);
	wait_writer (&writer, 0);
	writer = start_writer ("--check", MAIL_SCHEMA, "layout", "0");
	release_writer (&writer);
	wait_writer (&writer, 0);
}

static void
test_lock_failure (void)
{
	gchar *lockname = g_build_filename (g_get_user_config_dir (), "glib-2.0",
		"settings", "keyfile.lock", NULL);
	gchar *before, *after;
	Writer writer;

	g_assert_true (g_file_get_contents (filename, &before, NULL, NULL));
	if (g_unlink (lockname) == -1)
		g_assert_cmpint (errno, ==, ENOENT);
	g_assert_cmpint (g_mkdir (lockname, 0700), ==, 0);
	writer = start_writer ("--write", MAIL_SCHEMA, "paned-size", "100");
	release_writer (&writer);
	wait_writer (&writer, 2);
	g_assert_true (g_file_get_contents (filename, &after, NULL, NULL));
	g_assert_cmpstr (before, ==, after);
	g_assert_cmpint (g_rmdir (lockname), ==, 0);
	g_free (lockname);
	g_free (before);
	g_free (after);
}

static void
test_corrupt_file (void)
{
	gchar *original, *contents;
	const gchar *corrupt = "not a valid keyfile\n";
	Writer writer;

	g_assert_true (g_file_get_contents (filename, &original, NULL, NULL));
	g_assert_true (g_file_set_contents (filename, corrupt, -1, NULL));
	writer = start_writer ("--write", MAIL_SCHEMA, "paned-size", "100");
	release_writer (&writer);
	wait_writer (&writer, 2);
	g_assert_true (g_file_get_contents (filename, &contents, NULL, NULL));
	g_assert_cmpstr (contents, ==, corrupt);
	g_assert_true (g_file_set_contents (filename, original, -1, NULL));
	g_free (original);
	g_free (contents);
}

int
main (int argc,
      char **argv)
{
	gchar *directory, *settings_directory, *glib_directory, *lockname;
	gint result;

	if (argc == 5 && (g_str_equal (argv[1], "--write") || g_str_equal (argv[1], "--reset") ||
	    g_str_equal (argv[1], "--check"))) {
		GSettings *settings = g_settings_new (argv[2]);
		GVariant *value = g_variant_parse (NULL, argv[4], NULL, NULL, NULL);
		gchar go;
		gboolean success = TRUE;

		g_assert_nonnull (value);
		g_assert_cmpint (write (STDOUT_FILENO, "R", 1), ==, 1);
		g_assert_cmpint (read (STDIN_FILENO, &go, 1), ==, 1);
		if (g_str_equal (argv[1], "--check")) {
			GVariant *saved = g_settings_get_value (settings, argv[3]);

			success = g_variant_equal (saved, value);
			g_variant_unref (saved);
		} else if (g_str_equal (argv[1], "--reset"))
			g_settings_reset (settings, argv[3]);
		else
			success = g_settings_set_value (settings, argv[3], value);
		g_settings_sync ();
		g_variant_unref (value);
		g_object_unref (settings);
		return success ? 0 : 2;
	}

	directory = g_dir_make_tmp ("blueevolution-settings-XXXXXX", NULL);
	g_assert_nonnull (directory);
	g_setenv ("XDG_CONFIG_HOME", directory, TRUE);
	g_setenv ("GSETTINGS_BACKEND", "blueevolution-keyfile", TRUE);
	executable = g_canonicalize_filename (argv[0], NULL);
	glib_directory = g_build_filename (directory, "glib-2.0", NULL);
	settings_directory = g_build_filename (glib_directory, "settings", NULL);
	filename = g_build_filename (settings_directory, "keyfile", NULL);
	lockname = g_build_filename (settings_directory, "keyfile.lock", NULL);
	g_assert_cmpint (g_mkdir_with_parents (settings_directory, 0700), ==, 0);
	g_assert_true (g_file_set_contents (filename,
		"[io/github/themetalleg/BlueEvolution]\n"
		"mail-tracking-site-url='https://tracking.example.test'\n"
		"mail-tracking-enabled=true\n"
		"mail-tracking-default=true\n"
		"[org/gnome/evolution/mail]\nlayout=0\n"
		"[unknown/group]\nkeep='unchanged'\n", -1, NULL));

	g_test_init (&argc, &argv, NULL);
	alarm (30);
	g_test_add_func ("/settings/existing-profile", test_existing_profile);
	g_test_add_func ("/settings/stale-writer", test_stale_writer);
	g_test_add_func ("/settings/simultaneous-writers", test_simultaneous_writers);
	g_test_add_func ("/settings/delayed-write", test_delayed_write);
	g_test_add_func ("/settings/reset", test_reset);
	g_test_add_func ("/settings/notification", test_notification);
	g_test_add_func ("/settings/restart", test_restart);
	g_test_add_func ("/settings/lock-failure", test_lock_failure);
	g_test_add_func ("/settings/corrupt-file", test_corrupt_file);
	result = g_test_run ();

	g_unlink (filename);
	g_unlink (lockname);
	g_rmdir (settings_directory);
	g_rmdir (glib_directory);
	g_rmdir (directory);
	g_free (filename);
	g_free (lockname);
	g_free (settings_directory);
	g_free (glib_directory);
	g_free (directory);
	g_free (executable);
	return result;
}
