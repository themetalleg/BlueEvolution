#include "evolution-config.h"

#include <time.h>

#include "e-mail-list-cell.c"
#include "e-mail-request.c"
#define PROP_0 MESSAGE_LIST_PROP_0
#define N_PROPS MESSAGE_LIST_N_PROPS
#define properties message_list_properties
#include "message-list.c"
#undef properties
#undef N_PROPS
#undef PROP_0

static void
test_collapsed_thread_latest_message (void)
{
	CamelMessageInfo *first = camel_message_info_new (NULL);
	CamelMessageInfo *reply = camel_message_info_new (NULL);
	CamelMessageInfo *latest = camel_message_info_new (NULL);
	GNode *root = g_node_new (first);
	GNode *child = g_node_append_data (root, reply);
	camel_message_info_set_uid (first, "first");
	camel_message_info_set_uid (reply, "reply");
	camel_message_info_set_uid (latest, "latest");
	camel_message_info_set_date_received (first, 100);
	camel_message_info_set_date_received (reply, 200);
	camel_message_info_set_date_received (latest, 300);
	g_node_append_data (child, latest);
	g_assert_true (message_list_thread_message_info (root, TRUE) == latest);
	g_assert_true (message_list_thread_message_info (root, FALSE) == first);
	g_assert_true (message_list_thread_message_info (child, TRUE) == latest);
	g_assert_true (message_list_thread_message_info (child, FALSE) == reply);
	camel_message_info_set_date_received (latest, 0);
	camel_message_info_set_date_sent (latest, 400);
	g_assert_true (message_list_thread_message_info (root, TRUE) == latest);
	g_node_destroy (root);
	g_object_unref (first);
	g_object_unref (reply);
	g_object_unref (latest);
}

typedef struct { GObject parent; } WidthModel;
typedef GObjectClass WidthModelClass;
static GType width_model_get_type (void);
static void width_model_interface_init (ETableModelInterface *interface);
G_DEFINE_TYPE_WITH_CODE (WidthModel, width_model, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_TABLE_MODEL, width_model_interface_init))

static gint width_model_rows (ETableModel *model) { return 1; }
static gint width_model_columns (ETableModel *model) { return 2; }
static void width_model_init (WidthModel *model) {}
static void width_model_class_init (WidthModelClass *class) {}

static void
width_model_interface_init (ETableModelInterface *interface)
{
	interface->row_count = width_model_rows;
	interface->column_count = width_model_columns;
}

typedef struct { ECell parent; gint drawn_width; gint drawn_y1; gint drawn_y2; } WidthCell;
typedef ECellClass WidthCellClass;
static GType width_cell_get_type (void);
G_DEFINE_TYPE (WidthCell, width_cell, E_TYPE_CELL)

static gint
width_cell_measure (ECellView *view, gint model_column, gint view_column, gint row)
{
	return 90;
}

static void
width_cell_draw (ECellView *view, cairo_t *context, gint model_column, gint view_column,
                 gint row, ECellFlags flags, gint x1, gint y1, gint x2, gint y2)
{
	((WidthCell *) view->ecell)->drawn_width = x2 - x1;
	((WidthCell *) view->ecell)->drawn_y1 = y1;
	((WidthCell *) view->ecell)->drawn_y2 = y2;
}

static void width_cell_init (WidthCell *cell) {}

static void
width_cell_class_init (WidthCellClass *class)
{
	class->draw = width_cell_draw;
	class->height = width_cell_measure;
	class->max_width_by_row = width_cell_measure;
}

static void
test_widths (void)
{
	ECellHbox *box = E_CELL_HBOX (e_cell_hbox_new ());
	WidthCell *sender = g_object_new (width_cell_get_type (), NULL);
	WidthCell *date = g_object_new (width_cell_get_type (), NULL);
	ETableModel *model = g_object_new (width_model_get_type (), NULL);
	ECellView sender_view = { .ecell = E_CELL (sender), .e_table_model = model };
	ECellView date_view = { .ecell = E_CELL (date), .e_table_model = model };
	ECellView *children[] = { &sender_view, &date_view };
	ECellHboxView view = { 0 };
	cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *context = cairo_create (surface);
	const gint widths[] = { 220, 320, 520 };
	guint index;

	e_cell_hbox_append (box, E_CELL (sender), 0, 100);
	e_cell_hbox_append_natural (box, E_CELL (date), 1);
	view.cell_view.ecell = E_CELL (box);
	view.cell_view.e_table_model = model;
	view.subcell_view_count = 2;
	view.subcell_views = children;
	view.model_cols = box->model_cols;
	view.def_size_cols = box->def_size_cols;
	for (index = 0; index < G_N_ELEMENTS (widths); index++) {
		e_cell_draw ((ECellView *) &view, context, 0, 0, 0, 0, 0, 0, widths[index], 20);
		g_assert_cmpint (date->drawn_width, ==, 90);
		g_assert_cmpint (sender->drawn_width, ==, widths[index] - 90);
	}
	box->def_size_cols[0] = 60;
	box->def_size_cols[1] = 40;
	e_cell_draw ((ECellView *) &view, context, 0, 0, 0, 0, 0, 0, 300, 20);
	g_assert_cmpint (sender->drawn_width, ==, 180);
	g_assert_cmpint (date->drawn_width, ==, 120);
	cairo_destroy (context);
	cairo_surface_destroy (surface);
	g_object_unref (box);
	g_object_unref (sender);
	g_object_unref (date);
	g_object_unref (model);
}

static void
test_date_headings_only_for_top_level_rows (void)
{
	GNode *tree = g_node_new (NULL);
	GNode *thread = g_node_new (NULL);
	GNode *reply = g_node_new (NULL);

	g_node_append (tree, thread);
	g_node_append (thread, reply);
	g_assert_true (mail_list_is_top_level_row (thread));
	g_assert_false (mail_list_is_top_level_row (reply));
	g_assert_cmpint (mail_list_node_indent (thread), ==, 0);
	g_assert_cmpint (mail_list_node_indent (reply), ==, 20);
	{
		GNode *nested = g_node_append_data (reply, NULL);
		g_assert_cmpint (mail_list_node_indent (nested), ==, 40);
		for (guint depth = 0; depth < 8; depth++)
			nested = g_node_append_data (nested, NULL);
		g_assert_cmpint (mail_list_node_indent (nested), ==, 80);
	}
	g_node_destroy (tree);
}

typedef struct { GObject parent; gboolean unread; } RowModel;
typedef GObjectClass RowModelClass;
static GType row_model_get_type (void);
static void row_model_interface_init (ETableModelInterface *interface);
G_DEFINE_TYPE_WITH_CODE (RowModel, row_model, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_TABLE_MODEL, row_model_interface_init))

static void row_model_init (RowModel *model) { model->unread = TRUE; }
static void row_model_class_init (RowModelClass *class) {}
static gint row_model_columns (ETableModel *model) { return 3; }
static gpointer row_model_value (ETableModel *model, gint column, gint row)
{
	return column == 0 ? g_strdup ("Synthetic Sender") : GINT_TO_POINTER (((RowModel *) model)->unread);
}
static void row_model_free_value (ETableModel *model, gint column, gpointer value)
{
	if (column == 0)
		g_free (value);
}
static void row_model_interface_init (ETableModelInterface *interface)
{
	interface->row_count = width_model_rows;
	interface->column_count = row_model_columns;
	interface->value_at = row_model_value;
	interface->free_value = row_model_free_value;
}

static guint32
test_surface_pixel (cairo_surface_t *surface, gint x, gint y)
{
	guint32 pixel;

	cairo_surface_flush (surface);
	memcpy (&pixel, cairo_image_surface_get_data (surface) + y * cairo_image_surface_get_stride (surface) + x * 4, sizeof (pixel));
	return pixel;
}

static void
test_unread_row_background (void)
{
	GtkWidget *canvas = gnome_canvas_new ();
	GnomeCanvasItem *item;
	GtkCssProvider *provider = gtk_css_provider_new ();
	RowModel *model = g_object_new (row_model_get_type (), NULL);
	WidthCell *content = g_object_new (width_cell_get_type (), NULL);
	ECell *cell = e_mail_list_cell_new (E_CELL (content), 0, 2, 1);
	ECellView content_view = { .ecell = E_CELL (content), .e_table_model = E_TABLE_MODEL (model) };
	EMailListCellView view = { 0 };
	const gchar *palettes[] = {
		"@define-color theme_base_color #2d2d2d; @define-color theme_text_color #eeeeec;",
		"@define-color theme_base_color #ffffff; @define-color theme_text_color #202020;"
	};
	guint index;

	g_object_ref_sink (canvas);
	item = gnome_canvas_item_new (gnome_canvas_root (GNOME_CANVAS (canvas)), E_TYPE_TABLE_ITEM, NULL);
	view.parent.ecell = cell;
	view.parent.e_table_model = E_TABLE_MODEL (model);
	view.parent.e_table_item_view = item;
	view.content = &content_view;
	gtk_style_context_add_provider (gtk_widget_get_style_context (canvas), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (index = 0; index < G_N_ELEMENTS (palettes); index++) {
		cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 320, 90);
		cairo_t *context = cairo_create (surface);
		guint32 unread_pixel, selected_pixel;

		gtk_css_provider_load_from_data (provider, palettes[index], -1, NULL);
		e_cell_draw ((ECellView *) &view, context, 0, 0, 0, 0, 0, 0, 320, 90);
		g_assert_cmpint (content->drawn_y1, ==, 12);
		g_assert_cmpint (content->drawn_y2, ==, 78);
		g_assert_cmpint (e_cell_height ((ECellView *) &view, 0, 0, 0), ==, 114);
		unread_pixel = test_surface_pixel (surface, 80, 5);
		g_assert_cmpuint (unread_pixel >> 24, ==, 255);
		if (index == 0) {
			g_assert_cmpuint (unread_pixel & 255, >, 45);
			g_assert_cmpuint (unread_pixel & 255, <, 75);
		} else {
			g_assert_cmpuint (unread_pixel & 255, >, 235);
			g_assert_cmpuint (unread_pixel & 255, <, 255);
		}
		cairo_set_source_rgb (context, 0.1, 0.4, 0.7);
		cairo_paint (context);
		selected_pixel = test_surface_pixel (surface, 80, 5);
		e_cell_draw ((ECellView *) &view, context, 0, 0, 0, E_CELL_SELECTED, 0, 0, 320, 90);
		g_assert_cmphex (test_surface_pixel (surface, 80, 5), ==, selected_pixel);
		cairo_destroy (context);
		cairo_surface_destroy (surface);
	}
	g_object_unref (cell);
	g_object_unref (content);
	g_object_unref (model);
	g_object_unref (provider);
	gtk_widget_destroy (canvas);
	g_object_unref (canvas);
}

static void
test_expander_placement (void)
{
	GdkRectangle left, right;

	mail_list_expander_bounds (320, 90, 0, FALSE, &left);
	mail_list_expander_bounds (320, 122, 32, TRUE, &right);
	g_assert_cmpint (left.x, ==, 12);
	g_assert_cmpint (right.x, ==, 320 - 36);
	g_assert_cmpint (right.y - left.y, ==, 32);
	g_assert_cmpint (left.width, ==, 24);
	g_assert_cmpint (left.height, ==, 24);
	g_assert_cmpint (left.y + left.height, <=, 90);
	g_assert_cmpint (left.y, >, 90 / 2 + 4);
}

static void
test_attachment_symbolic_foreground (void)
{
	const gchar *icons[] = { NULL, "mail-attachment-symbolic" };
	const gchar *palettes[] = { "* { color: #202020; }", "* { color: #eeeeee; }", "* { color: #202020; }" };
	GtkWidget *canvas = gnome_canvas_new ();
	GtkCssProvider *provider = gtk_css_provider_new ();
	RowModel *model = g_object_new (row_model_get_type (), NULL);
	ECell *cell = e_cell_toggle_new (icons, G_N_ELEMENTS (icons));
	ECellView *view;
	guint index;

	g_object_ref_sink (canvas);
	view = e_cell_new_view (cell, E_TABLE_MODEL (model), gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (canvas)), E_TYPE_TABLE_ITEM, NULL));
	gtk_style_context_add_provider (gtk_widget_get_style_context (canvas), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	for (index = 0; index < G_N_ELEMENTS (palettes); index++) {
		cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 32, 24);
		cairo_t *context = cairo_create (surface);
		guint pixels = 0, brightness = 0;
		gint pixel_x, pixel_y;

		gtk_css_provider_load_from_data (provider, palettes[index], -1, NULL);
		e_cell_draw (view, context, 1, 0, 0, 0, 0, 0, 32, 24);
		for (pixel_y = 0; pixel_y < 24; pixel_y++) {
			for (pixel_x = 0; pixel_x < 32; pixel_x++) {
				guint32 pixel = test_surface_pixel (surface, pixel_x, pixel_y);

				if ((pixel >> 24) > 200) {
					brightness += (pixel & 255) * 255 / (pixel >> 24);
					pixels++;
				}
			}
		}
		g_assert_cmpuint (pixels, >, 5);
		if (index == 1)
			g_assert_cmpuint (brightness / pixels, >, 220);
		else
			g_assert_cmpuint (brightness / pixels, <, 50);
		cairo_destroy (context);
		cairo_surface_destroy (surface);
	}
	e_cell_kill_view (view);
	g_object_unref (cell);
	g_object_unref (model);
	g_object_unref (provider);
	gtk_widget_destroy (canvas);
	g_object_unref (canvas);
}

static void
test_older_dates_use_abbreviated_weekday (void)
{
	GTimeZone *zone = g_time_zone_new_identifier ("Europe/Zurich");
	GDateTime *now = g_date_time_new (zone, 2026, 10, 21, 15, 0, 0);
	GDateTime *date = g_date_time_new (zone, 2026, 10, 14, 15, 0, 0);
	gchar *weekday = g_date_time_format (date, "%a");
	gchar *short_date = g_date_time_format (date, "%x");
	gchar *expected = g_strdup_printf ("%s, %s", weekday, short_date);
	gchar *actual = mail_list_compact_date (g_date_time_to_unix (date), now);

	g_assert_cmpstr (actual, ==, expected);
	g_free (actual);
	g_free (expected);
	g_free (short_date);
	g_free (weekday);
	g_date_time_unref (date);
	g_date_time_unref (now);
	g_time_zone_unref (zone);
}

static void
test_attachment_stream_foreground (void)
{
	GIcon *icon = g_themed_icon_new ("mail-attachment-symbolic");
	const gchar *colors[] = { "#202020", "#eeeeee", "#202020" };
	guint index;

	for (index = 0; index < G_N_ELEMENTS (colors); index++) {
		GOutputStream *output = g_memory_output_stream_new_resizable ();
		GInputStream *input;
		GBytes *bytes;
		GdkPixbuf *pixbuf;
		GdkRGBA foreground;
		gchar *mime_type = NULL;
		guint pixels = 0, brightness = 0;
		gint pixel_x, pixel_y;

		gdk_rgba_parse (&foreground, colors[index]);
		save_gicon_to_stream (icon, 32, &foreground, output, &mime_type);
		g_assert_cmpstr (mime_type, ==, "image/png");
		g_assert_true (g_output_stream_close (output, NULL, NULL));
		bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
		input = g_memory_input_stream_new_from_bytes (bytes);
		pixbuf = gdk_pixbuf_new_from_stream (input, NULL, NULL);
		g_assert_nonnull (pixbuf);
		g_assert_cmpint (gdk_pixbuf_get_width (pixbuf), ==, 32);
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
		if (index == 1)
			g_assert_cmpuint (brightness / pixels, >, 220);
		else
			g_assert_cmpuint (brightness / pixels, <, 50);
		g_object_unref (pixbuf);
		g_object_unref (input);
		g_bytes_unref (bytes);
		g_object_unref (output);
		g_free (mime_type);
	}
	g_object_unref (icon);
}

static void
test_attachment_stream_preserves_image (void)
{
	GdkPixbuf *original = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
	GdkPixbuf *loaded;
	GOutputStream *output = g_memory_output_stream_new_resizable ();
	GInputStream *input;
	GBytes *bytes, *image_bytes;
	GIcon *icon;
	GdkRGBA foreground = { 1, 1, 1, 1 };
	gchar *buffer, *mime_type = NULL;
	gsize length;
	const guchar *pixel;

	gdk_pixbuf_fill (original, 0x118833ff);
	g_assert_true (gdk_pixbuf_save_to_buffer (original, &buffer, &length, "png", NULL, NULL));
	image_bytes = g_bytes_new_take (buffer, length);
	icon = g_bytes_icon_new (image_bytes);
	save_gicon_to_stream (icon, 16, &foreground, output, &mime_type);
	g_assert_cmpstr (mime_type, ==, "image/png");
	g_assert_true (g_output_stream_close (output, NULL, NULL));
	bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
	input = g_memory_input_stream_new_from_bytes (bytes);
	loaded = gdk_pixbuf_new_from_stream (input, NULL, NULL);
	g_assert_nonnull (loaded);
	pixel = gdk_pixbuf_read_pixels (loaded);
	g_assert_cmpuint (pixel[0], ==, 0x11);
	g_assert_cmpuint (pixel[1], ==, 0x88);
	g_assert_cmpuint (pixel[2], ==, 0x33);
	g_object_unref (loaded);
	g_object_unref (input);
	g_bytes_unref (bytes);
	g_object_unref (icon);
	g_bytes_unref (image_bytes);
	g_object_unref (output);
	g_object_unref (original);
	g_free (mime_type);
}

static void
test_thread_expand_default_closed (void)
{
	GSettings *settings = e_util_ref_settings ("org.gnome.evolution.mail");
	GVariant *default_value = g_settings_get_default_value (settings, "thread-expand");

	g_assert_nonnull (default_value);
	g_assert_false (g_variant_get_boolean (default_value));
	g_variant_unref (default_value);
	g_object_unref (settings);
}

static void
check_group (GTimeZone *zone, GDateTime *now, gint year, gint month, gint day, gint expected)
{
	GDateTime *date = g_date_time_new (zone, year, month, day, 12, 0, 0);

	g_assert_cmpint (mail_list_date_group (g_date_time_to_unix (date), now), ==, expected);
	g_date_time_unref (date);
}

static void
test_dates (void)
{
	GTimeZone *zone = g_time_zone_new_identifier ("Europe/Zurich");
	GDateTime *now = g_date_time_new (zone, 2026, 10, 21, 15, 0, 0);
	GDateTime *boundary, *previous;
	GSettings *settings = e_util_ref_settings ("org.gnome.evolution.calendar");
	gchar *text;

	check_group (zone, now, 2026, 10, 22, 0);
	check_group (zone, now, 2026, 10, 21, 1);
	check_group (zone, now, 2026, 10, 20, 2);
	check_group (zone, now, 2026, 10, 19, 3);
	check_group (zone, now, 2026, 10, 15, 4);
	check_group (zone, now, 2026, 10, 2, 5);
	check_group (zone, now, 2026, 9, 15, 6);
	check_group (zone, now, 2026, 8, 15, 7);
	g_assert_cmpint (mail_list_date_group (0, now), ==, 8);
	g_settings_set_boolean (settings, "use-24hour-format", TRUE);
	text = mail_list_compact_date (g_date_time_to_unix (now), now);
	g_assert_cmpstr (text, ==, "15:00");
	g_free (text);
	g_settings_set_boolean (settings, "use-24hour-format", FALSE);
	text = mail_list_compact_date (g_date_time_to_unix (now), now);
	g_assert_nonnull (strstr (text, "03:00"));
	g_assert_null (strstr (text, "15:00"));
	g_free (text);
	g_object_unref (settings);
	g_date_time_unref (now);
	boundary = g_date_time_new (zone, 2026, 3, 30, 0, 30, 0);
	previous = g_date_time_new (zone, 2026, 3, 29, 23, 30, 0);
	g_assert_cmpint (mail_list_date_group (g_date_time_to_unix (previous), boundary), ==, 2);
	g_date_time_unref (previous);
	g_date_time_unref (boundary);
	boundary = g_date_time_new (zone, 2026, 10, 26, 0, 30, 0);
	previous = g_date_time_new (zone, 2026, 10, 25, 23, 30, 0);
	g_assert_cmpint (mail_list_date_group (g_date_time_to_unix (previous), boundary), ==, 2);
	g_date_time_unref (previous);
	g_date_time_unref (boundary);
	boundary = g_date_time_new (zone, 2026, 1, 15, 12, 0, 0);
	check_group (zone, boundary, 2025, 12, 20, 6);
	g_date_time_unref (boundary);
	boundary = g_date_time_new (zone, 2024, 3, 1, 12, 0, 0);
	check_group (zone, boundary, 2024, 2, 29, 2);
	g_date_time_unref (boundary);
	g_time_zone_unref (zone);
}

int
main (int argument_count, char **argument_values)
{
	g_setenv ("TZ", "Europe/Zurich", TRUE);
	tzset ();
	gtk_init (&argument_count, &argument_values);
	g_test_init (&argument_count, &argument_values, NULL);
	g_test_add_func ("/blueevolution/mail/date-width", test_widths);
	g_test_add_func ("/blueevolution/mail/date-headings-top-level", test_date_headings_only_for_top_level_rows);
	g_test_add_func ("/blueevolution/mail/older-date-weekday-abbreviated", test_older_dates_use_abbreviated_weekday);
	g_test_add_func ("/blueevolution/mail/thread-expand-default-closed", test_thread_expand_default_closed);
	g_test_add_func ("/blueevolution/mail/collapsed-thread-latest-message", test_collapsed_thread_latest_message);
	g_test_add_func ("/blueevolution/mail/date-sections", test_dates);
	g_test_add_func ("/blueevolution/mail/unread-row-background", test_unread_row_background);
	g_test_add_func ("/blueevolution/mail/expander-under-initials", test_expander_placement);
	g_test_add_func ("/blueevolution/mail/attachment-symbolic-foreground", test_attachment_symbolic_foreground);
	g_test_add_func ("/blueevolution/mail/attachment-stream-foreground", test_attachment_stream_foreground);
	g_test_add_func ("/blueevolution/mail/attachment-stream-preserves-image", test_attachment_stream_preserves_image);
	return g_test_run ();
}