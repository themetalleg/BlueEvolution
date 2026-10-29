# Local Email Samples

This directory is a local-only test library. Message files are excluded by
`.gitignore` and must not be committed or uploaded; they may contain personal or
untrusted email content.

The VS Code **BlueEvolution: Preview Light/Dark** tasks import `.mbox` and
`.eml` files from this directory into **On This Computer/Example E-Mails** and
copy those messages into the local **Inbox** for rendering review. Preview runs
do not seed the bundled synthetic mail when these files are present. Existing
bundled synthetic messages are removed from the preview Inbox; other messages
are preserved. Imported files are tracked by content hash in the private
preview cache, so relaunching does not duplicate them. These messages stay in
the local mail store and are not synced to a mail server.

`scroll-test.mbox` contains 20 synthetic, independent messages with numbered
subjects for the **BlueEvolution: Preview Dark** (or Light) task. Use them to
compare single wheel notches, rapid scrolling, direction changes, and touch
scrolling. Wheel notches animate briefly when GTK animations are enabled;
high-resolution mouse wheels are smoothed as well. Touch input and modified
wheel gestures retain GTK's native behavior.

For manual imports outside the preview, use **File > Import > Import a single
file** for `.mbox`, or open an `.eml` with BlueEvolution and choose **Import to
Folder**.

Keep this directory for samples you want to reuse when checking message
rendering. Do not place passwords, API keys, private keys, or other credentials
here.