#ifndef E_MAIL_LIST_CELL_H
#define E_MAIL_LIST_CELL_H

#include <e-util/e-util.h>

G_BEGIN_DECLS

ECell *e_mail_list_cell_new (ECell *content, gint sender_column, gint date_column, gint unread_column);
ECell *e_mail_list_date_cell_new (void);
ECell *e_mail_list_preview_cell_new (void);
ECell *e_mail_list_status_cell_new (void);

G_END_DECLS

#endif