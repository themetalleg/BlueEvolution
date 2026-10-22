#include "evolution-config.h"

#include <glib/gi18n.h>
#include <string.h>

#include "e-mail-list-cell.h"

typedef struct {
	ECell parent;
	ECell *content;
	gint sender_column;
	gint date_column;
} EMailListCell;

typedef ECellClass EMailListCellClass;

typedef struct {
	ECellView parent;
	ECellView *content;
} EMailListCellView;

typedef struct {
	ECellText parent;
	gboolean date;
} EMailListTextCell;

typedef ECellTextClass EMailListTextCellClass;

static GType e_mail_list_cell_get_type (void);
static GType e_mail_list_text_cell_get_type (void);

G_DEFINE_TYPE (EMailListCell, e_mail_list_cell, E_TYPE_CELL)
G_DEFINE_TYPE (EMailListTextCell, e_mail_list_text_cell, E_TYPE_CELL_TEXT)

static gint
mail_list_day_distance (GDateTime *now,
                       GDateTime *date)
{
	GDate today, message_date;

	g_date_clear (&today, 1);
	g_date_clear (&message_date, 1);
	g_date_set_dmy (&today, g_date_time_get_day_of_month (now),
		g_date_time_get_month (now), g_date_time_get_year (now));
	g_date_set_dmy (&message_date, g_date_time_get_day_of_month (date),
		g_date_time_get_month (date), g_date_time_get_year (date));
	return (gint) g_date_get_julian (&today) - (gint) g_date_get_julian (&message_date);
}

static gint
mail_list_date_group (gint64 timestamp,
                      GDateTime *now)
{
	GDateTime *date, *last_month;
	gint days, group;

	date = timestamp ? g_date_time_new_from_unix_local (timestamp) : NULL;
	if (!date)
		return 8;
	days = mail_list_day_distance (now, date);
	if (days < 0)
		group = 0;
	else if (days == 0)
		group = 1;
	else if (days == 1)
		group = 2;
	else if (days < g_date_time_get_day_of_week (now))
		group = 3;
	else if (days < g_date_time_get_day_of_week (now) + 7)
		group = 4;
	else if (g_date_time_get_year (now) == g_date_time_get_year (date) &&
	         g_date_time_get_month (now) == g_date_time_get_month (date))
		group = 5;
	else {
		last_month = g_date_time_add_months (now, -1);
		group = g_date_time_get_year (last_month) == g_date_time_get_year (date) &&
			g_date_time_get_month (last_month) == g_date_time_get_month (date) ? 6 : 7;
		g_date_time_unref (last_month);
	}
	g_date_time_unref (date);
	return group;
}

static gchar *
mail_list_compact_date (gint64 timestamp,
                        GDateTime *now)
{
	GDateTime *date;
	gchar *result, *time_text, *weekday, *date_text;
	gint days;

	date = timestamp ? g_date_time_new_from_unix_local (timestamp) : NULL;
	if (!date)
		return g_strdup (_("Unknown date"));
	days = mail_list_day_distance (now, date);
	if (days >= 0 && days < 7) {
		GSettings *settings = e_util_ref_settings ("org.gnome.evolution.calendar");
		gboolean use_24hour = g_settings_get_boolean (settings, "use-24hour-format");

		time_text = g_date_time_format (date, use_24hour ? "%H:%M" : "%I:%M %p");
		g_object_unref (settings);
		if (days == 0)
			result = g_strdup (time_text);
		else {
			weekday = g_date_time_format (date, "%a");
			result = g_strdup_printf ("%s, %s", weekday, time_text);
			g_free (weekday);
		}
		g_free (time_text);
	} else {
		weekday = g_date_time_format (date, "%a");
		date_text = g_date_time_format (date, "%x");
		result = g_strdup_printf ("%s, %s", weekday, date_text);
		g_free (weekday);
		g_free (date_text);
	}
	g_date_time_unref (date);
	return result;
}

static gchar *
mail_list_text_get (ECellText *text_cell,
                   ETableModel *model,
                   gint column,
                   gint row)
{
	EMailListTextCell *cell = (EMailListTextCell *) text_cell;
	gpointer value = e_table_model_value_at (model, column, row);
	gchar *result;

	if (cell->date) {
		GDateTime *now = g_date_time_new_now_local ();

		result = mail_list_compact_date (value ? *((gint64 *) value) : 0, now);
		g_date_time_unref (now);
	} else {
		result = g_markup_printf_escaped ("<span alpha='65%%'>%s</span>", value ? (gchar *) value : "");
	}
	e_table_model_free_value (model, column, value);
	return result;
}

static void
mail_list_text_free (ECellText *cell,
                    ETableModel *model,
                    gint column,
                    gchar *text)
{
	g_free (text);
}

static void
e_mail_list_text_cell_class_init (EMailListTextCellClass *class)
{
	class->get_text = mail_list_text_get;
	class->free_text = mail_list_text_free;
}

static void
e_mail_list_text_cell_init (EMailListTextCell *cell)
{
}

ECell *
e_mail_list_date_cell_new (void)
{
	EMailListTextCell *cell = g_object_new (e_mail_list_text_cell_get_type (), NULL);

	cell->date = TRUE;
	e_cell_text_construct (E_CELL_TEXT (cell), NULL, GTK_JUSTIFY_RIGHT);
	g_object_set (cell, "use-tabular-numbers", TRUE, NULL);
	return E_CELL (cell);
}

ECell *
e_mail_list_preview_cell_new (void)
{
	EMailListTextCell *cell = g_object_new (e_mail_list_text_cell_get_type (), NULL);

	e_cell_text_construct (E_CELL_TEXT (cell), NULL, GTK_JUSTIFY_LEFT);
	g_object_set (cell, "is-markup", TRUE, NULL);
	return E_CELL (cell);
}

static gint64
mail_list_timestamp (ECellView *view,
                     gint column,
                     gint row)
{
	gint64 *value = e_table_model_value_at (view->e_table_model, column, row);
	gint64 timestamp = value ? *value : 0;

	e_table_model_free_value (view->e_table_model, column, value);
	return timestamp;
}

static gboolean
mail_list_is_top_level_row (GNode *node)
{
	return node != NULL && node->parent != NULL && G_NODE_IS_ROOT (node->parent);
}

static gint
mail_list_heading (ECellView *view,
                   gint row)
{
	ETableSortInfo *sort_info;
	ETableColumnSpecification *sort_column;
	GtkSortType direction;
	GDateTime *now;
	gint current_group, previous_group;
	EMailListCell *cell = (EMailListCell *) view->ecell;

	if (row < 0 || !E_IS_TREE_TABLE_ADAPTER (view->e_table_model))
		return -1;
	if (!mail_list_is_top_level_row (e_tree_table_adapter_node_at_row (
		E_TREE_TABLE_ADAPTER (view->e_table_model), row)))
		return -1;
	sort_info = e_tree_table_adapter_get_sort_info (E_TREE_TABLE_ADAPTER (view->e_table_model));
	if (!e_table_sort_info_sorting_get_count (sort_info))
		return -1;
	sort_column = e_table_sort_info_sorting_get_nth (sort_info, 0, &direction);
	if (!sort_column || sort_column->model_col != cell->date_column)
		return -1;
	now = g_date_time_new_now_local ();
	current_group = mail_list_date_group (mail_list_timestamp (view, cell->date_column, row), now);
	previous_group = row > 0 ?
		mail_list_date_group (mail_list_timestamp (view, cell->date_column, row - 1), now) : -1;
	g_date_time_unref (now);
	return current_group != previous_group ? current_group : -1;
}

static GtkWidget *
mail_list_canvas (ECellView *view)
{
	return GTK_WIDGET (GNOME_CANVAS_ITEM (view->e_table_item_view)->canvas);
}

static ECellView *
mail_list_new_view (ECell *ecell,
                    ETableModel *model,
                    gpointer item)
{
	EMailListCell *cell = (EMailListCell *) ecell;
	EMailListCellView *view = g_new0 (EMailListCellView, 1);

	view->parent.ecell = ecell;
	view->parent.e_table_model = model;
	view->parent.e_table_item_view = item;
	view->content = e_cell_new_view (cell->content, model, item);
	return (ECellView *) view;
}

static void
mail_list_kill_view (ECellView *view)
{
	EMailListCellView *list_view = (EMailListCellView *) view;

	if (view->kill_view_cb)
		view->kill_view_cb (view, view->kill_view_cb_data);
	g_list_free (view->kill_view_cb_data);
	e_cell_kill_view (list_view->content);
	g_free (view);
}

static void
mail_list_realize (ECellView *view)
{
	e_cell_realize (((EMailListCellView *) view)->content);
}

static void
mail_list_unrealize (ECellView *view)
{
	e_cell_unrealize (((EMailListCellView *) view)->content);
}

static gint
mail_list_height (ECellView *view,
                  gint model_column,
                  gint view_column,
                  gint row)
{
	return e_cell_height (((EMailListCellView *) view)->content, model_column, view_column, row) +
		16 + (mail_list_heading (view, row) >= 0 ? 32 : 0);
}

static void
mail_list_draw (ECellView *view,
                cairo_t *context,
                gint model_column,
                gint view_column,
                gint row,
                ECellFlags flags,
                gint x1,
                gint y1,
                gint x2,
                gint y2)
{
	const gchar *headings[] = {
		N_("Upcoming"), N_("Today"), N_("Yesterday"), N_("This Week"),
		N_("Last Week"), N_("Earlier This Month"), N_("Last Month"), N_("Older"), N_("Unknown date")
	};
	const gchar *colors[] = { "#8640a4", "#0f6cbd", "#9a5400", "#008272", "#c23978", "#5b5fc7" };
	EMailListCell *cell = (EMailListCell *) view->ecell;
	GtkWidget *canvas = mail_list_canvas (view);
	PangoLayout *layout;
	PangoAttrList *attributes;
	PangoRectangle initials_ink;
	GdkRGBA foreground, background, accent;
	gchar *sender, *initial_text;
	const gchar *cursor;
	gunichar first = 0, last = 0;
	gchar initials[16] = { 0 };
	gboolean word_start = TRUE;
	gboolean rtl = gtk_widget_get_direction (canvas) == GTK_TEXT_DIR_RTL;
	gint heading = mail_list_heading (view, row);
	gint heading_height = heading >= 0 ? 32 : 0;
	gint text_width, text_height, initial_length;
	gint avatar_x = rtl ? x2 - 24 : x1 + 24;
	gint avatar_y = y1 + heading_height + (y2 - y1 - heading_height) / 2;

	cairo_save (context);
	cairo_rectangle (context, x1, y1, MAX (0, x2 - x1), MAX (0, y2 - y1));
	cairo_clip (context);
	e_utils_get_theme_color (canvas, "theme_text_color,theme_fg_color", "#202020", &foreground);
	e_utils_get_theme_color (canvas, "theme_base_color", "#ffffff", &background);
	if (heading >= 0) {
		gdk_cairo_set_source_rgba (context, &background);
		cairo_rectangle (context, x1, y1, x2 - x1, heading_height);
		cairo_fill (context);
		layout = gtk_widget_create_pango_layout (canvas, _(headings[heading]));
		attributes = pango_attr_list_new ();
		pango_attr_list_insert (attributes, pango_attr_weight_new (PANGO_WEIGHT_SEMIBOLD));
		pango_layout_set_attributes (layout, attributes);
		pango_attr_list_unref (attributes);
		pango_layout_get_pixel_size (layout, &text_width, &text_height);
		gdk_cairo_set_source_rgba (context, &foreground);
		cairo_move_to (context, rtl ? x2 - 12 - text_width : x1 + 12,
			y1 + (heading_height - text_height) / 2);
		pango_cairo_show_layout (context, layout);
		g_object_unref (layout);
	}

	sender = e_table_model_value_at (view->e_table_model, cell->sender_column, row);
	for (cursor = sender ? sender : ""; *cursor; cursor = g_utf8_next_char (cursor)) {
		gunichar character = g_utf8_get_char (cursor);

		if (word_start && g_unichar_isalnum (character)) {
			if (!first)
				first = g_unichar_toupper (character);
			else
				last = g_unichar_toupper (character);
		}
		word_start = g_unichar_isspace (character);
	}
	initial_length = g_unichar_to_utf8 (first ? first : '?', initials);
	if (last)
		g_unichar_to_utf8 (last, initials + initial_length);
	gdk_rgba_parse (&accent, colors[g_str_hash (sender ? sender : "") % G_N_ELEMENTS (colors)]);
	gdk_cairo_set_source_rgba (context, &accent);
	cairo_arc (context, avatar_x, avatar_y, 16, 0, 2 * G_PI);
	cairo_fill (context);
	initial_text = g_markup_printf_escaped ("<span weight='semibold' size='small'>%s</span>", initials);
	layout = gtk_widget_create_pango_layout (canvas, NULL);
	pango_layout_set_markup (layout, initial_text, -1);
	pango_layout_get_pixel_extents (layout, &initials_ink, NULL);
	cairo_set_source_rgb (context, 1, 1, 1);
	cairo_move_to (context, avatar_x - initials_ink.width / 2 - initials_ink.x,
		avatar_y - initials_ink.height / 2 - initials_ink.y);
	pango_cairo_show_layout (context, layout);
	g_object_unref (layout);
	g_free (initial_text);
	e_table_model_free_value (view->e_table_model, cell->sender_column, sender);

	e_cell_draw (((EMailListCellView *) view)->content, context, model_column, view_column, row,
		flags, x1 + (rtl ? 12 : 48), y1 + heading_height + 8,
		x2 - (rtl ? 48 : 12), y2 - 8);
	foreground.alpha = 0.16;
	gdk_cairo_set_source_rgba (context, &foreground);
	cairo_set_line_width (context, 1);
	cairo_move_to (context, x1, y2 - 0.5);
	cairo_line_to (context, x2, y2 - 0.5);
	cairo_stroke (context);
	cairo_restore (context);
}

static gint
mail_list_event (ECellView *view,
                 GdkEvent *event,
                 gint model_column,
                 gint view_column,
                 gint row,
                 ECellFlags flags,
                 ECellActions *actions)
{
	EMailListCellView *list_view = (EMailListCellView *) view;
	GdkEvent *translated;
	gdouble event_x, event_y;
	gint result, heading_height = mail_list_heading (view, row) >= 0 ? 32 : 0;
	gint inset = gtk_widget_get_direction (mail_list_canvas (view)) == GTK_TEXT_DIR_RTL ? 12 : 48;

	if (!gdk_event_get_coords (event, &event_x, &event_y))
		return e_cell_event (list_view->content, event, model_column, view_column, row, flags, actions);
	if (event_y < heading_height || event_x < inset)
		return 0;
	translated = gdk_event_copy (event);
	if (event->type == GDK_MOTION_NOTIFY) {
		translated->motion.x -= inset;
		translated->motion.y -= heading_height + 8;
	} else if (event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE ||
	           event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS) {
		translated->button.x -= inset;
		translated->button.y -= heading_height + 8;
	}
	result = e_cell_event (list_view->content, translated, model_column, view_column, row, flags, actions);
	gdk_event_free (translated);
	return result;
}

static gint
mail_list_width (ECellView *view,
                 gint model_column,
                 gint view_column)
{
	return e_cell_max_width (((EMailListCellView *) view)->content, model_column, view_column) + 60;
}

static void
mail_list_dispose (GObject *object)
{
	g_clear_object (&((EMailListCell *) object)->content);
	G_OBJECT_CLASS (e_mail_list_cell_parent_class)->dispose (object);
}

static void
e_mail_list_cell_class_init (EMailListCellClass *class)
{
	G_OBJECT_CLASS (class)->dispose = mail_list_dispose;
	class->new_view = mail_list_new_view;
	class->kill_view = mail_list_kill_view;
	class->realize = mail_list_realize;
	class->unrealize = mail_list_unrealize;
	class->height = mail_list_height;
	class->draw = mail_list_draw;
	class->event = mail_list_event;
	class->max_width = mail_list_width;
}

static void
e_mail_list_cell_init (EMailListCell *cell)
{
}

ECell *
e_mail_list_cell_new (ECell *content,
                      gint sender_column,
                      gint date_column)
{
	EMailListCell *cell = g_object_new (e_mail_list_cell_get_type (), NULL);

	cell->content = g_object_ref (content);
	cell->sender_column = sender_column;
	cell->date_column = date_column;
	return E_CELL (cell);
}