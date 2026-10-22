#include "evolution-config.h"

#include "e-util.h"
#include "e-color-scheme-watcher.c"

static void
check_dark_preference (gboolean expected)
{
	gboolean actual = !expected;

	g_object_get (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", &actual, NULL);
	g_assert_cmpint (actual, ==, expected);
}

static void
test_appearance_precedence (void)
{
	EColorSchemeWatcher watcher = { 0 };

	g_object_set (gtk_settings_get_default (), "gtk-theme-name", "Adwaita", NULL);
	watcher.appearance_settings = g_settings_new ("io.github.themetalleg.BlueEvolution");
	watcher.color_scheme = E_COLOR_SCHEME_PREFER_DARK;
	g_settings_set_string (watcher.appearance_settings, "appearance-mode", "light");
	e_color_scheme_watcher_sync_theme (&watcher);
	check_dark_preference (FALSE);
	e_color_scheme_watcher_set_color_scheme (&watcher, E_COLOR_SCHEME_PREFER_LIGHT);
	e_color_scheme_watcher_set_color_scheme (&watcher, E_COLOR_SCHEME_PREFER_DARK);
	check_dark_preference (FALSE);
	g_settings_set_string (watcher.appearance_settings, "appearance-mode", "dark");
	e_color_scheme_watcher_set_color_scheme (&watcher, E_COLOR_SCHEME_PREFER_LIGHT);
	check_dark_preference (TRUE);
	g_settings_set_string (watcher.appearance_settings, "appearance-mode", "system");
	e_color_scheme_watcher_sync_theme (&watcher);
	check_dark_preference (FALSE);
	e_color_scheme_watcher_set_color_scheme (&watcher, E_COLOR_SCHEME_PREFER_DARK);
	check_dark_preference (TRUE);
	e_color_scheme_watcher_set_color_scheme (&watcher, E_COLOR_SCHEME_PREFER_LIGHT);
	check_dark_preference (FALSE);
	g_object_unref (watcher.appearance_settings);
}

static void
test_appearance_preference_without_theme_name (void)
{
	EColorSchemeWatcher watcher = { 0 };
	GtkSettings *gtk_settings = gtk_settings_get_default ();
	gchar *theme_name = NULL;

	g_object_get (gtk_settings, "gtk-theme-name", &theme_name, NULL);
	g_object_set (gtk_settings, "gtk-theme-name", NULL, NULL);
	watcher.appearance_settings = g_settings_new ("io.github.themetalleg.BlueEvolution");
	watcher.color_scheme = E_COLOR_SCHEME_UNKNOWN;
	g_settings_set_string (watcher.appearance_settings, "appearance-mode", "dark");
	e_color_scheme_watcher_sync_theme (&watcher);
	check_dark_preference (TRUE);
	g_settings_set_string (watcher.appearance_settings, "appearance-mode", "light");
	e_color_scheme_watcher_sync_theme (&watcher);
	check_dark_preference (FALSE);

	g_object_set (gtk_settings, "gtk-theme-name", theme_name, NULL);
	g_free (theme_name);
	g_object_unref (watcher.appearance_settings);
}

static void
test_ui_definitions (void)
{
	const gchar *names[] = { "evolution-shell.eui", "evolution-mail.eui", "evolution-mail-reader.eui" };
	EUIParser *parser = e_ui_parser_new ();
	guint index;

	for (index = 0; index < G_N_ELEMENTS (names); index++) {
		GError *error = NULL;
		gchar *contents = NULL;
		gchar *filename = g_build_filename (EVOLUTION_UIDIR, names[index], NULL);
		gsize length = 0;

		if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
			g_free (filename);
			filename = g_build_filename (EVOLUTION_TESTTOPSRCDIR, "data", "ui", names[index], NULL);
		}
		g_assert_true (g_file_get_contents (filename, &contents, &length, &error));
		g_assert_no_error (error);
		g_assert_true (e_ui_parser_merge_data (parser, contents, length, &error));
		g_assert_no_error (error);
		g_free (contents);
		g_free (filename);
	}
	g_object_unref (parser);
}

static void
test_bootstrap_aliases (void)
{
	GtkIconTheme *theme = gtk_icon_theme_new ();
	GDir *directory;
	const gchar *filename;
	guint count = 0;

	gtk_icon_theme_prepend_search_path (theme, EVOLUTION_ICONDIR);
	gtk_icon_theme_set_custom_theme (theme, "BlueEvolution");
	directory = g_dir_open (EVOLUTION_ICONDIR "/BlueEvolution/scalable/actions", 0, NULL);
	g_assert_nonnull (directory);
	while ((filename = g_dir_read_name (directory))) {
		GtkIconInfo *info;
		gchar *name;

		if (!g_str_has_suffix (filename, "-symbolic.svg"))
			continue;
		name = g_strndup (filename, strlen (filename) - 4);
		info = gtk_icon_theme_lookup_icon (theme, name, 24, GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
		g_assert_nonnull (info);
		g_assert_true (g_str_has_prefix (gtk_icon_info_get_filename (info), EVOLUTION_ICONDIR "/BlueEvolution/"));
		g_object_unref (info);
		g_free (name);
		count++;
	}
	g_assert_cmpuint (count, ==, 290);
	g_dir_close (directory);
	g_object_unref (theme);
}

int
main (gint argc, gchar **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/blueevolution/appearance/preference-precedence", test_appearance_precedence);
	g_test_add_func ("/blueevolution/appearance/preference-without-theme-name", test_appearance_preference_without_theme_name);
	g_test_add_func ("/blueevolution/ui/definitions", test_ui_definitions);
	g_test_add_func ("/blueevolution/icons/bootstrap-aliases", test_bootstrap_aliases);
	return g_test_run ();
}