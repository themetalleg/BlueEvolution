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
check_toolbar_icon_items (EUIElement *element)
{
	guint index;

	if (e_ui_element_get_kind (element) == E_UI_ELEMENT_KIND_ITEM)
		g_assert_false (e_ui_element_item_get_important (element));

	for (index = 0; index < e_ui_element_get_n_children (element); index++)
		check_toolbar_icon_items (e_ui_element_get_child (element, index));
}

static void
collect_toolbar_actions (EUIElement *element,
			 GPtrArray *actions)
{
	guint index;

	if (e_ui_element_get_kind (element) == E_UI_ELEMENT_KIND_ITEM)
		g_ptr_array_add (actions, (gpointer) e_ui_element_item_get_action (element));

	for (index = 0; index < e_ui_element_get_n_children (element); index++)
		collect_toolbar_actions (e_ui_element_get_child (element, index), actions);
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
		if (index > 0) {
			const gchar *toolbars[] = { "<toolbar id='mail-preview-toolbar'", "<toolbar id='main-toolbar-with-headerbar'", "<toolbar id='main-toolbar-without-headerbar'" };
			guint toolbar_index;
			for (toolbar_index = index == 1 ? 0 : 1; toolbar_index < (index == 1 ? 1 : 3); toolbar_index++) {
				const gchar *start = strstr (contents, toolbars[toolbar_index]);
				const gchar *end;
				g_assert_nonnull (start);
				end = strstr (start, "</toolbar>");
				g_assert_nonnull (end);
				g_assert_nonnull (g_strstr_len (start, end - start, "action='mail-mark-current-read'"));
			}
		}
		g_free (contents);
		g_free (filename);
	}
	{
		const gchar *toolbars[] = { "main-toolbar-with-headerbar", "main-toolbar-without-headerbar", "mail-preview-toolbar" };
		guint toolbar_index;

		for (toolbar_index = 0; toolbar_index < G_N_ELEMENTS (toolbars); toolbar_index++) {
			EUIElement *toolbar = e_ui_element_get_child_by_id (e_ui_parser_get_root (parser), toolbars[toolbar_index]);
			g_assert_nonnull (toolbar);
			check_toolbar_icon_items (toolbar);
			if (toolbar_index < 2) {
				GPtrArray *actions = g_ptr_array_new ();
				guint stop_index, send_receive_index, spacer_index, delete_index;

				collect_toolbar_actions (toolbar, actions);
				g_assert_true (g_ptr_array_find_with_equal_func (actions, "mail-stop", g_str_equal, &stop_index));
				g_assert_true (g_ptr_array_find_with_equal_func (actions, "EMailShellView::mail-send-receive", g_str_equal, &send_receive_index));
				g_assert_true (g_ptr_array_find_with_equal_func (actions, "EMailShellView::mail-toolbar-spacer", g_str_equal, &spacer_index));
				g_assert_true (g_ptr_array_find_with_equal_func (actions, "mail-delete", g_str_equal, &delete_index));
				g_assert_cmpuint (stop_index, <, send_receive_index);
				g_assert_cmpuint (send_receive_index, <, spacer_index);
				g_assert_cmpuint (spacer_index, <, delete_index);
				g_ptr_array_unref (actions);
			}
		}
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
test_window_icon_palette (void)
{
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkCssProvider *provider = gtk_css_provider_new ();
	const gchar *palettes[] = {
		"@define-color theme_fg_color #202020;",
		"@define-color theme_fg_color #eeeeee;",
		"@define-color theme_fg_color #202020;"
	};
	guint index;

	gtk_style_context_add_provider (gtk_widget_get_style_context (window), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (index = 0; index < G_N_ELEMENTS (palettes); index++) {
		GList *icons, *link;

		gtk_css_provider_load_from_data (provider, palettes[index], -1, NULL);
		if (index == 0)
			e_icon_factory_set_window_icon (GTK_WINDOW (window), "mail-message-new");
		else
			g_signal_emit_by_name (window, "style-updated");
		icons = gtk_window_get_icon_list (GTK_WINDOW (window));
		g_assert_cmpuint (g_list_length (icons), ==, 3);
		for (link = icons; link; link = g_list_next (link))
			check_icon_brightness (GDK_PIXBUF (link->data), index == 1);
		g_list_free (icons);
	}
	e_icon_factory_set_window_icon (GTK_WINDOW (window), "blueevolution");
	g_assert_cmpstr (gtk_window_get_icon_name (GTK_WINDOW (window)), ==, "blueevolution");
	e_icon_factory_set_window_icon (GTK_WINDOW (window), NULL);
	g_assert_null (gtk_window_get_icon_name (GTK_WINDOW (window)));
	g_object_unref (provider);
	gtk_widget_destroy (window);
}

static void
test_regular_icon_audit (void)
{
	if (g_test_subprocess ()) {
		GtkIconTheme *theme = gtk_icon_theme_new ();
		GtkIconInfo *info;

		g_setenv ("BLUEEVOLUTION_ICON_AUDIT", "strict", TRUE);
		gtk_icon_theme_prepend_search_path (theme, EVOLUTION_ICONDIR);
		gtk_icon_theme_set_custom_theme (theme, "BlueEvolution");
		info = gtk_icon_theme_lookup_icon (theme, "mail-message-new", 16, GTK_ICON_LOOKUP_FORCE_REGULAR);
		g_assert_nonnull (info);
		e_icon_factory_load_regular_icon (info, NULL);
		g_object_unref (info);
		g_object_unref (theme);
		return;
	}
	g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_failed ();
	g_test_trap_assert_stderr ("*Unthemed UI icon reached the regular-image loader*");
}

static void
test_contact_placeholder_icon (void)
{
	gboolean prefer_symbolic = e_icon_factory_get_prefer_symbolic_icons ();
	GtkWidget *chooser;
	GList *children;
	GtkImage *image;
	GdkPixbuf *photo = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
	gchar *buffer;
	gsize length;

	e_icon_factory_set_prefer_symbolic_icons (TRUE);
	chooser = e_image_chooser_new ("avatar-default");
	g_object_ref_sink (chooser);
	children = gtk_container_get_children (GTK_CONTAINER (chooser));
	g_assert_cmpuint (g_list_length (children), ==, 1);
	image = GTK_IMAGE (children->data);
	g_assert_true (GTK_IS_IMAGE (image));
	g_assert_cmpint (gtk_image_get_storage_type (image), ==, GTK_IMAGE_ICON_NAME);
	g_assert_false (e_image_chooser_has_image (E_IMAGE_CHOOSER (chooser)));
	gdk_pixbuf_fill (photo, 0x118833ff);
	g_assert_true (gdk_pixbuf_save_to_buffer (photo, &buffer, &length, "png", NULL, NULL));
	g_assert_true (e_image_chooser_set_image_data (E_IMAGE_CHOOSER (chooser), buffer, length));
	g_assert_true (e_image_chooser_has_image (E_IMAGE_CHOOSER (chooser)));
	g_assert_cmpint (gtk_image_get_storage_type (image), ==, GTK_IMAGE_PIXBUF);
	e_image_chooser_unset_image (E_IMAGE_CHOOSER (chooser));
	g_assert_false (e_image_chooser_has_image (E_IMAGE_CHOOSER (chooser)));
	g_assert_cmpint (gtk_image_get_storage_type (image), ==, GTK_IMAGE_ICON_NAME);
	g_free (buffer);
	g_object_unref (photo);
	g_list_free (children);
	gtk_widget_destroy (chooser);
	g_object_unref (chooser);
	e_icon_factory_set_prefer_symbolic_icons (prefer_symbolic);
}

static void
test_online_button_icon (void)
{
	GtkWidget *button = e_online_button_new ();
	GList *children;
	GtkWidget *image;
	const gchar *name;
	GtkIconSize size;

	g_object_ref_sink (button);
	children = gtk_container_get_children (GTK_CONTAINER (button));
	g_assert_cmpuint (g_list_length (children), ==, 1);
	image = children->data;
	if (GTK_IS_CONTAINER (image)) {
		GList *images = gtk_container_get_children (GTK_CONTAINER (image));

		g_assert_nonnull (images);
		image = images->data;
		g_list_free (images);
	}
	g_assert_true (GTK_IS_IMAGE (image));
	e_online_button_set_online (E_ONLINE_BUTTON (button), TRUE);
	g_assert_cmpint (gtk_image_get_storage_type (GTK_IMAGE (image)), ==, GTK_IMAGE_ICON_NAME);
	gtk_image_get_icon_name (GTK_IMAGE (image), &name, &size);
	g_assert_cmpstr (name, ==, "network-transmit-receive-symbolic");
	g_assert_true (gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), name));
	e_online_button_set_online (E_ONLINE_BUTTON (button), FALSE);
	gtk_image_get_icon_name (GTK_IMAGE (image), &name, &size);
	g_assert_cmpstr (name, ==, "network-offline-symbolic");
	g_assert_true (gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), name));
	g_assert_cmpint (gtk_image_get_pixel_size (GTK_IMAGE (image)), ==, 16);
	g_list_free (children);
	gtk_widget_destroy (button);
	g_object_unref (button);
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
	g_test_add_func ("/blueevolution/icons/window-palette", test_window_icon_palette);
	g_test_add_func ("/blueevolution/icons/regular-loader-audit", test_regular_icon_audit);
	g_test_add_func ("/blueevolution/icons/contact-placeholder", test_contact_placeholder_icon);
	g_test_add_func ("/blueevolution/icons/online-button", test_online_button_icon);
	return g_test_run ();
}