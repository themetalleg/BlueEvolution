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
test_appearance_rendered_palette (void)
{
	EColorSchemeWatcher watcher = { 0 };
	GtkSettings *settings = gtk_settings_get_default ();
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *entry = gtk_entry_new ();
	const gchar *modes[] = { "light", "dark", "light" };
	gboolean animations;
	guint index;

	g_object_get (settings, "gtk-enable-animations", &animations, NULL);
	g_object_set (settings, "gtk-theme-name", "Breeze", "gtk-enable-animations", FALSE, NULL);
	watcher.appearance_settings = g_settings_new ("io.github.themetalleg.BlueEvolution");
	gtk_container_add (GTK_CONTAINER (window), entry);
	gtk_widget_show_all (window);
	for (index = 0; index < G_N_ELEMENTS (modes); index++) {
		cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 120, 40);
		cairo_t *context = cairo_create (surface);
		guint32 pixel;
		guint red, green, blue;

		g_settings_set_string (watcher.appearance_settings, "appearance-mode", modes[index]);
		e_color_scheme_watcher_sync_theme (&watcher);
		while (g_main_context_iteration (NULL, FALSE));
		gtk_render_background (gtk_widget_get_style_context (entry), context, 0, 0, 120, 40);
		cairo_surface_flush (surface);
		memcpy (&pixel, cairo_image_surface_get_data (surface) + 20 * cairo_image_surface_get_stride (surface) + 60 * 4, sizeof (pixel));
		red = (pixel >> 16) & 255;
		green = (pixel >> 8) & 255;
		blue = pixel & 255;
		g_assert_cmpuint (pixel >> 24, ==, 255);
		if (g_str_equal (modes[index], "dark")) {
			g_assert_cmpuint (red, <, 90);
			g_assert_cmpuint (green, <, 90);
			g_assert_cmpuint (blue, <, 90);
		} else {
			g_assert_cmpuint (red, >, 180);
			g_assert_cmpuint (green, >, 180);
			g_assert_cmpuint (blue, >, 180);
		}
		cairo_destroy (context);
		cairo_surface_destroy (surface);
	}
	gtk_widget_destroy (window);
	g_object_set (settings, "gtk-enable-animations", animations, NULL);
	g_object_unref (watcher.appearance_settings);
}

static void
check_icon_brightness (GdkPixbuf *pixbuf,
                       gboolean light)
{
	guint pixels = 0, brightness = 0;
	gint pixel_x, pixel_y;

	g_assert_nonnull (pixbuf);
	for (pixel_y = 0; pixel_y < gdk_pixbuf_get_height (pixbuf); pixel_y++) {
		for (pixel_x = 0; pixel_x < gdk_pixbuf_get_width (pixbuf); pixel_x++) {
			const guchar *pixel = gdk_pixbuf_read_pixels (pixbuf) +
				pixel_y * gdk_pixbuf_get_rowstride (pixbuf) + pixel_x * gdk_pixbuf_get_n_channels (pixbuf);

			if (!gdk_pixbuf_get_has_alpha (pixbuf) || pixel[3] > 200) {
				brightness += pixel[0];
				pixels++;
			}
		}
	}
	g_assert_cmpuint (pixels, >, 5);
	if (light)
		g_assert_cmpuint (brightness / pixels, >, 180);
	else
		g_assert_cmpuint (brightness / pixels, <, 100);
}

static void
test_icon_factory_palette (void)
{
	GtkSettings *settings = gtk_settings_get_default ();
	gboolean original_dark;
	gboolean prefer_symbolic = e_icon_factory_get_prefer_symbolic_icons ();
	gchar *original_theme;
	guint index;

	g_object_get (settings, "gtk-theme-name", &original_theme, "gtk-application-prefer-dark-theme", &original_dark, NULL);
	e_icon_factory_set_prefer_symbolic_icons (TRUE);
	for (index = 0; index < 3; index++) {
		GdkPixbuf *pixbuf;

		g_object_set (settings, "gtk-theme-name", "Adwaita", "gtk-application-prefer-dark-theme", index == 1, NULL);
		while (g_main_context_iteration (NULL, FALSE));
		pixbuf = e_icon_factory_get_icon ("mail-attachment-symbolic", GTK_ICON_SIZE_MENU);
		check_icon_brightness (pixbuf, index == 1);
		g_object_unref (pixbuf);
	}
	e_icon_factory_set_prefer_symbolic_icons (prefer_symbolic);
	g_object_set (settings, "gtk-theme-name", original_theme, "gtk-application-prefer-dark-theme", original_dark, NULL);
	g_free (original_theme);
}

static void
test_table_heading_palette (void)
{
	GtkWidget *widget = gtk_label_new (NULL);
	GtkCssProvider *provider = gtk_css_provider_new ();
	ETableCol *column = g_object_new (E_TYPE_TABLE_COL, NULL);
	gboolean prefer_symbolic = e_icon_factory_get_prefer_symbolic_icons ();
	const gchar *palettes[] = { "* { color: #202020; }", "* { color: #eeeeee; }", "* { color: #202020; }" };
	guint index;

	g_object_ref_sink (widget);
	e_icon_factory_set_prefer_symbolic_icons (TRUE);
	column->icon_name = g_strdup ("mail-attachment");
	gtk_style_context_add_provider (gtk_widget_get_style_context (widget), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (index = 0; index < G_N_ELEMENTS (palettes); index++) {
		GdkPixbuf *pixbuf;

		gtk_css_provider_load_from_data (provider, palettes[index], -1, NULL);
		e_table_col_ensure_surface (column, widget);
		g_assert_nonnull (column->surface);
		pixbuf = gdk_pixbuf_get_from_surface (column->surface, 0, 0, column->surface_width, column->surface_height);
		check_icon_brightness (pixbuf, index == 1);
		g_object_unref (pixbuf);
	}
	g_object_unref (column);
	e_icon_factory_set_prefer_symbolic_icons (prefer_symbolic);
	g_object_unref (provider);
	gtk_widget_destroy (widget);
	g_object_unref (widget);
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
	g_test_add_func ("/blueevolution/appearance/rendered-palette", test_appearance_rendered_palette);
	g_test_add_func ("/blueevolution/ui/definitions", test_ui_definitions);
	g_test_add_func ("/blueevolution/icons/bootstrap-aliases", test_bootstrap_aliases);
	g_test_add_func ("/blueevolution/icons/factory-palette", test_icon_factory_palette);
	g_test_add_func ("/blueevolution/icons/table-heading-palette", test_table_heading_palette);
	return g_test_run ();
}