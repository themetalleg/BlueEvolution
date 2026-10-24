'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');

assert.match(process.env.INPUT_PUBLICATION_SHA ?? '', /^[a-f0-9]{40}$/);
assert.ok(process.env.GITHUB_WORKSPACE);
process.env.GITHUB_SHA = process.env.INPUT_PUBLICATION_SHA;
require(path.join(process.env.GITHUB_WORKSPACE, '.pages-deploy-action/dist/index.js'));