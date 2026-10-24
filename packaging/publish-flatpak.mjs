import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { appendFileSync, createReadStream, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { assertNewerRelease, releaseMetadata, writeInstallFiles } from './release.mjs';

function run(command, args, options = {}) {
  return execFileSync(command, args, { encoding: 'utf8', stdio: ['pipe', 'pipe', 'inherit'], ...options });
}

function api(path, method = 'GET', body) {
  const args = ['api', path, '--method', method];
  if (body) args.push('--input', '-');
  return JSON.parse(run('gh', args, { input: body ? JSON.stringify(body) : undefined }));
}

async function checksum(filename) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(filename)) hash.update(chunk);
  return hash.digest('hex');
}

export function nextPublicationState(metadata, previous, fingerprint) {
  assert.deepEqual(metadata, releaseMetadata(metadata.version));
  if (previous) assert.equal(previous.signing_fingerprint, fingerprint, 'Signing-key changes require an explicit key-rotation migration');
  assertNewerRelease(metadata.version, previous?.channels[metadata.channel]?.version);
  return {
    schema: 1, latest_tag: metadata.tag, signing_fingerprint: fingerprint,
    archive_name: 'BlueEvolution-updates.tar.gz',
    channels: { ...previous?.channels, [metadata.channel]: { version: metadata.version, tag: metadata.tag } }
  };
}

export function mergeRepository(repository, incoming, metadata, publicKey, fingerprint) {
  mkdirSync(repository, { recursive: true });
  if (!existsSync(join(repository, 'config'))) run('ostree', ['init', `--repo=${repository}`, '--mode=archive-z2']);
  const refs = new Set(run('ostree', ['refs', `--repo=${repository}`]).trim().split('\n'));
  const history = `history/${metadata.app_id}`;
  for (const [source, target] of [[`${history}/1`, `${history}/2`], [metadata.ref, `${history}/1`]]) {
    if (refs.has(source)) run('ostree', ['refs', `--repo=${repository}`, '--force', `--create=${target}`, source]);
  }
  run('ostree', ['remote', 'add', `--repo=${repository}`, '--if-not-exists', `--gpg-import=${publicKey}`,
    'release', pathToFileURL(incoming).href]);
  run('ostree', ['pull-local', `--repo=${repository}`, '--remote=release', '--gpg-verify', '--untrusted', incoming, metadata.ref]);
  run('ostree', ['refs', `--repo=${repository}`, '--force', `--create=${metadata.ref}`, `release:${metadata.ref}`]);
  run('ostree', ['remote', 'delete', `--repo=${repository}`, 'release']);
  run('flatpak', ['build-update-repo', '--title=BlueEvolution', '--default-branch=stable',
    `--gpg-import=${publicKey}`, `--gpg-sign=${fingerprint}`, '--prune', '--prune-depth=3', repository]);
  run('ostree', ['fsck', `--repo=${repository}`]);
}

function stateHead(repository) {
  const refs = api(`repos/${repository}/git/matching-refs/heads/flatpak-state`);
  return refs.find(ref => ref.ref === 'refs/heads/flatpak-state')?.object.sha ?? null;
}

async function prepare(root, repository, tag, baseUrl) {
  assert.ok(tag.startsWith('blueevolution-v'));
  const metadata = releaseMetadata(tag.slice('blueevolution-v'.length));
  const release = api(`repos/${repository}/releases/tags/${encodeURIComponent(tag)}`);
  assert.equal(release.draft, false, 'Only published, approved releases can update users');
  assert.equal(release.prerelease, metadata.prerelease, 'Release prerelease flag does not match its version/channel');
  const previousSha = stateHead(repository);
  const previous = previousSha ? JSON.parse(Buffer.from(
    api(`repos/${repository}/contents/state.json?ref=${previousSha}`).content, 'base64')) : null;
  const alreadyPublished = previous?.channels[metadata.channel]?.tag === tag;
  const redeploy = process.env.REDEPLOY_RELEASE === 'true';
  if (alreadyPublished && !redeploy) {
    if (process.env.GITHUB_OUTPUT) appendFileSync(process.env.GITHUB_OUTPUT, 'skip=true\n');
    console.log(`${tag} is already published to ${metadata.channel}`);
    return;
  }
  const fingerprint = run('gpg', ['--batch', '--with-colons', '--fingerprint', process.env.FLATPAK_GPG_KEY_ID])
    .split('\n').find(line => line.startsWith('fpr:'))?.split(':')[9];
  assert.match(fingerprint ?? '', /^[A-F0-9]{40,64}$/);
  if (alreadyPublished) {
    assert.equal(previous.latest_tag, tag, 'Only the latest repository snapshot can be redeployed');
    assert.equal(previous.signing_fingerprint, fingerprint);
  }
  const state = alreadyPublished ? { ...previous } : nextPublicationState(metadata, previous, fingerprint);
  const candidate = join(root, 'candidate');
  const site = join(root, 'site');
  const repo = join(site, 'repo');
  mkdirSync(candidate, { recursive: true });
  mkdirSync(repo, { recursive: true });
  const download = (releaseTag, asset, directory) => run('gh', ['release', 'download', releaseTag,
    '--repo', repository, '--pattern', asset, '--dir', directory]);
  const archive = `BlueEvolution-${metadata.version}-repository.tar.gz`;
  for (const asset of [archive, `${archive}.sha256`, 'release.json']) download(tag, asset, candidate);
  assert.deepEqual(JSON.parse(readFileSync(join(candidate, 'release.json'))), metadata);
  run('sha256sum', ['--check', `${archive}.sha256`], { cwd: candidate });
  if (previous) {
    download(previous.latest_tag, previous.archive_name, root);
    assert.equal(await checksum(join(root, previous.archive_name)), previous.archive_sha256, 'Saved repository checksum mismatch');
    run('tar', ['-xzf', join(root, previous.archive_name), '-C', repo]);
  }
  const incoming = join(root, 'incoming');
  mkdirSync(incoming);
  run('tar', ['-xzf', join(candidate, archive), '-C', incoming]);
  const keyFile = join(site, 'signing-key.gpg');
  run('gpg', ['--batch', '--export', '--output', keyFile, fingerprint]);
  mergeRepository(repo, incoming, metadata, keyFile, fingerprint);
  writeInstallFiles(site, baseUrl, readFileSync(keyFile), state.channels);
  const bytes = Number(run('du', ['-sb', site]).split(/\s/)[0]);
  assert.ok(bytes < 950_000_000, 'Update repository exceeds the GitHub Pages size budget');
  const snapshot = join(root, 'snapshot');
  mkdirSync(snapshot);
  const savedArchive = join(snapshot, state.archive_name);
  run('tar', ['-czf', savedArchive, '-C', repo, '.']);
  state.archive_sha256 = await checksum(savedArchive);
  writeFileSync(join(root, 'state.json'), `${JSON.stringify(state, null, 2)}\n`);
  writeFileSync(join(root, 'previous.json'), JSON.stringify(previousSha));
  const content = readFileSync(join(root, 'state.json'), 'utf8');
  const blob = api(`repos/${repository}/git/blobs`, 'POST', { content, encoding: 'utf-8' });
  const tree = api(`repos/${repository}/git/trees`, 'POST', {
    tree: [{ path: 'state.json', mode: '100644', type: 'blob', sha: blob.sha }]
  });
  const commit = api(`repos/${repository}/git/commits`, 'POST', {
    message: `Publish ${state.latest_tag} (${process.env.GITHUB_RUN_ID ?? 'manual'})`, tree: tree.sha,
    parents: previousSha ? [previousSha] : []
  });
  writeFileSync(join(root, 'publication-commit.json'), JSON.stringify(commit.sha));
  run('gh', ['release', 'upload', tag, savedArchive, '--repo', repository, '--clobber']);
  if (process.env.GITHUB_OUTPUT) appendFileSync(process.env.GITHUB_OUTPUT, `skip=false\npublication_sha=${commit.sha}\n`);
  console.log(`Prepared ${metadata.app_name} ${metadata.version}; repository is ${bytes} bytes`);
}

function record(root, repository) {
  const previousSha = JSON.parse(readFileSync(join(root, 'previous.json')));
  assert.equal(stateHead(repository), previousSha, 'Publication state changed during deployment; do not overwrite it');
  const state = JSON.parse(readFileSync(join(root, 'state.json'), 'utf8'));
  const commit = JSON.parse(readFileSync(join(root, 'publication-commit.json')));
  if (previousSha) api(`repos/${repository}/git/refs/heads/flatpak-state`, 'PATCH', { sha: commit, force: false });
  else api(`repos/${repository}/git/refs`, 'POST', { ref: 'refs/heads/flatpak-state', sha: commit });
  console.log(`Recorded deployed update repository for ${state.latest_tag}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const root = process.env.PUBLISH_ROOT;
  const repository = process.env.GITHUB_REPOSITORY;
  assert.ok(root && repository, 'PUBLISH_ROOT and GITHUB_REPOSITORY are required');
  mkdirSync(root, { recursive: true });
  if (process.argv[2] === 'prepare') await prepare(root, repository, process.env.RELEASE_TAG, process.env.PAGES_URL);
  else if (process.argv[2] === 'record') record(root, repository);
  else throw new Error('Expected prepare or record');
}