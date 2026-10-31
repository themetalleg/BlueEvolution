# Beta Release Notes

This beta adds optional WordPress email tracking. Evaluate it with a test
mailbox and the WordPress Tracking Pixel plugin before using it with live mail.

## Changes

- Configure an HTTPS WordPress site, project ID, and project API key under Settings > Integrations > Email Tracking. The key is stored in the desktop keyring.
- Enable tracking globally and choose whether new messages track by default; each message can override that choice in the composer Options menu.
- Review recorded image-request totals and first/latest request times in the preview of a tracked sent message.
- Registration failures stop sending. Tracking is unavailable for digitally signed or encrypted mail and requires an HTML body.

## Privacy and Limitations

- Image requests are not proof that a person read a message. Mail providers and privacy proxies can fetch or cache images independently of a recipient.
- Use a WordPress Tracking Pixel release that supports the project message resolver. See https://github.com/themetalleg/wordpress-tracking-pixel/releases/.
- Review message content and recipient expectations before enabling tracking.

This project is not affiliated with Microsoft or GNOME.