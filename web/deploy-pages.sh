#!/usr/bin/env bash
# Publish the browser build to GitHub Pages.
#
# The contents of web/ go to the root of a gh-pages branch, so the demo lives at
# https://<owner>.github.io/<repo>/ rather than a path several levels deep. Uses
# a detached worktree so the working tree is never touched.
set -euo pipefail
cd "$(dirname "$0")/.."

BRANCH="${BRANCH:-gh-pages}"
WORK="$(mktemp -d)"
trap 'git worktree remove --force "$WORK" 2>/dev/null || true; rm -rf "$WORK"' EXIT

# rebuild first, so what ships is what the current sources produce
if command -v em++ >/dev/null; then
  web/build.sh >/dev/null
  echo "rebuilt the wasm"
else
  echo "note: Emscripten not found, publishing the committed dist/ as-is"
fi

[ -f web/dist/swarm.wasm ] || { echo "web/dist/swarm.wasm missing -- run web/build.sh"; exit 1; }

if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
  git worktree add --quiet "$WORK" "$BRANCH"
else
  git worktree add --quiet --detach "$WORK"
  git -C "$WORK" checkout --orphan "$BRANCH"
  git -C "$WORK" rm -rq --cached . 2>/dev/null || true
fi

find "$WORK" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -r web/index.html web/src web/dist "$WORK"/
touch "$WORK/.nojekyll"          # keep Pages from mangling paths

cat > "$WORK/README.md" <<'EOF'
Generated branch — the browser build of
[smarmsynth](https://github.com/audiodestrukt/smarmsynth), published by
`web/deploy-pages.sh`. Edit the sources on `main`, not here.
EOF

git -C "$WORK" add -A
if git -C "$WORK" diff --cached --quiet; then
  echo "nothing changed"
else
  git -C "$WORK" -c user.name="${GIT_NAME:-dnewcome}" \
                 -c user.email="${GIT_EMAIL:-djn125@yahoo.com}" \
                 commit -q -m "Publish browser build from $(git rev-parse --short HEAD)"
  git -C "$WORK" push -q origin "$BRANCH"
  echo "pushed $BRANCH"
fi
