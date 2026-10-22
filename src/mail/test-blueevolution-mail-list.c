#include "evolution-config.h"

#include <time.h>

#include "e-mail-list-cell.c"

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

typedef struct { ECell parent; gint drawn_width; } WidthCell;
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
}

static void width_cell_init (WidthCell *cell) {}

static void
width_cell_class_init (WidthCellClass *class)
{
	class->draw = width_cell_draw;
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
	g_node_destroy (tree);
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
	g_test_init (&argument_count, &argument_values, NULL);
	g_test_add_func ("/blueevolution/mail/date-width", test_widths);
	g_test_add_func ("/blueevolution/mail/date-headings-top-level", test_date_headings_only_for_top_level_rows);
	g_test_add_func ("/blueevolution/mail/older-date-weekday-abbreviated", test_older_dates_use_abbreviated_weekday);
	g_test_add_func ("/blueevolution/mail/thread-expand-default-closed", test_thread_expand_default_closed);
	g_test_add_func ("/blueevolution/mail/date-sections", test_dates);
	return g_test_run ();
}