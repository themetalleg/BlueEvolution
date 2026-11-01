# Beta Release Notes

This beta adds optional WordPress email tracking. Evaluate it with a test
mailbox and the WordPress Tracking Pixel plugin before using it with live mail.

## Changes

- Connection testing now validates a generated pixel URL and write permissions as well as the API and resolver. It reuses one synthetic registration per project without sending mail or loading its pixel.
- Settings document WordPress's exact Site Address requirement, including `www`, port, and subdirectory matching.
- Replaced tracking banners with a compact count box beneath the remote-content control and a multiline timestamp tooltip. The visible image-request disclaimer was removed; privacy limitations remain documented below.
- Successful history snapshots are saved locally with private permissions and atomic writes. Opening a tracked message always requests fresh API data while keeping saved data available if the API fails.
- Counts, status messages, and timestamps are language-aware, with German translations and local-time formatting. Up to the latest 10,000 timestamps are listed; incomplete lists are marked.
- Saving the first WordPress API key now enables connection testing immediately. Tracking controls also refresh after changing options or removing a key, and testing is disabled while connection details are unsaved.
- WordPress URL, project ID, and API-key fields now expand across the settings page; URL and key fields have a wider baseline.
- Configure an HTTPS WordPress site, project ID, and project API key under Settings > Integrations > Email Tracking. The key is stored in the desktop keyring.
- Enable tracking globally and choose whether new messages track by default; each message can override that choice in the composer Options menu.
- Review recorded image-request totals and individual request timestamps in the preview of a tracked sent message.
- Registration failures stop sending. Tracking is unavailable for digitally signed or encrypted mail and requires an HTML body.

## Privacy and Limitations

- Image requests are not proof that a person read a message. Mail providers and privacy proxies can fetch or cache images independently of a recipient.
- Use a WordPress Tracking Pixel release that supports the project message resolver. See https://github.com/themetalleg/wordpress-tracking-pixel/releases/.
- Review message content and recipient expectations before enabling tracking.

This project is not affiliated with Microsoft or GNOME.