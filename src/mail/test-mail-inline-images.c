#include "evolution-config.h"

#include <em-format/e-mail-part-attachment.h>
#include <em-format/e-mail-part-image.h>
#include <em-format/e-mail-part-list.h>

#include "e-cid-request.h"

typedef struct {
	GObject parent;
	EMailPartList *parts;
} InlineResolver;

typedef GObjectClass InlineResolverClass;

static void inline_resolver_interface_init (ECidResolverInterface *iface);
GType inline_resolver_get_type (void);

G_DEFINE_TYPE_WITH_CODE (InlineResolver, inline_resolver, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (E_TYPE_CID_RESOLVER, inline_resolver_interface_init))

static CamelMimePart *
inline_resolver_ref_part (ECidResolver *resolver,
			  const gchar *uri)
{
	EMailPart *part;
	CamelMimePart *mime_part;

	part = e_mail_part_list_ref_part (((InlineResolver *) resolver)->parts, uri);
	mime_part = part ? e_mail_part_ref_mime_part (part) : NULL;
	g_clear_object (&part);

	return mime_part;
}

static void
inline_resolver_interface_init (ECidResolverInterface *iface)
{
	iface->ref_part = inline_resolver_ref_part;
}

static void
inline_resolver_class_init (InlineResolverClass *class)
{
}

static void
inline_resolver_init (InlineResolver *resolver)
{
}

static void
test_inline_cid_request (gconstpointer data)
{
	const gchar *content_id = data;
	const gchar payload[] = "Synthetic inline image bytes";
	CamelMimeMessage *message = camel_mime_message_new ();
	CamelMimePart *mime_part = camel_mime_part_new ();
	EMailPartList *parts = e_mail_part_list_new (message, "synthetic-inline", NULL);
	EMailPart *image, *attachment, *found;
	InlineResolver *resolver = g_object_new (inline_resolver_get_type (), NULL);
	EContentRequest *request = e_cid_request_new ();
	gchar *escaped = g_uri_escape_string (content_id, NULL, FALSE);
	gchar *uris[] = {
		g_strconcat ("cid:", content_id, NULL),
		g_strconcat ("cid://localhost/", escaped, NULL)
	};
	guint ii;

	camel_mime_part_set_content (mime_part, payload, strlen (payload), "image/png");
	camel_mime_part_set_content_id (mime_part, content_id);
	camel_mime_part_set_disposition (mime_part, "inline");
	camel_mime_part_set_filename (mime_part, "screenshot.png");
	image = e_mail_part_image_new (mime_part, ".image");
	attachment = E_MAIL_PART (e_mail_part_attachment_new (mime_part, ".attachment"));
	/* Named related images are queued after a wrapper with the same CID. */
	e_mail_part_list_add_part (parts, attachment);
	e_mail_part_list_add_part (parts, image);
	resolver->parts = parts;

	for (ii = 0; ii < G_N_ELEMENTS (uris); ii++) {
		GInputStream *stream = NULL;
		gchar *mime_type = NULL;
		gint64 length = -1;
		gsize read = 0;
		gchar buffer[sizeof payload] = { 0 };
		GError *error = NULL;

		found = e_mail_part_list_ref_part (parts, uris[ii]);
		g_assert_true (found == attachment);
		g_object_unref (found);
		g_assert_true (e_content_request_process_sync (request, uris[ii],
			G_OBJECT (resolver), &stream, &length, &mime_type, NULL, &error));
		g_assert_no_error (error);
		g_assert_cmpstr (mime_type, ==, "image/png");
		g_assert_cmpint (length, ==, strlen (payload));
		g_assert_true (g_input_stream_read_all (stream, buffer, sizeof buffer, &read, NULL, &error));
		g_assert_no_error (error);
		g_assert_cmpmem (buffer, read, payload, strlen (payload));
		g_object_unref (stream);
		g_free (mime_type);
	}

	g_assert_cmpstr (camel_mime_part_get_content_id (mime_part), ==, content_id);
	g_assert_cmpstr (e_mail_part_get_cid (image), ==, uris[0]);
	g_assert_null (e_mail_part_list_ref_part (parts, "cid://localhost/missing"));
	g_assert_null (e_mail_part_list_ref_part (parts, "cid://other-host/synthetic"));

	g_free (uris[0]);
	g_free (uris[1]);
	g_free (escaped);
	g_object_unref (request);
	g_object_unref (resolver);
	g_object_unref (attachment);
	g_object_unref (image);
	g_object_unref (parts);
	g_object_unref (mime_part);
	g_object_unref (message);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_data_func ("/blueevolution/mail/inline-cid", "screenshot@example.invalid", test_inline_cid_request);
	g_test_add_data_func ("/blueevolution/mail/inline-cid-escaped", "Screenshot+one/two?three#four%25@example.invalid", test_inline_cid_request);

	return g_test_run ();
}
