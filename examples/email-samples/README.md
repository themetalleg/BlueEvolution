# Local Email Samples

This directory is a local-only test library. Message files are excluded by
`.gitignore` and must not be committed or uploaded; they may contain personal or
untrusted email content.

The VS Code **BlueEvolution: Preview Light/Dark** tasks import `.mbox` and
`.eml` files from this directory into **On This Computer/Example E-Mails**.
Imported files are tracked by content hash in the private preview cache, so
relaunching the preview does not duplicate them. New files are imported on a
later preview launch. These messages stay in the local mail store and are not
synced to a mail server.

For manual imports outside the preview, use **File > Import > Import a single
file** for `.mbox`, or open an `.eml` with BlueEvolution and choose **Import to
Folder**.

Keep this directory for samples you want to reuse when checking message
rendering. Do not place passwords, API keys, private keys, or other credentials
here.