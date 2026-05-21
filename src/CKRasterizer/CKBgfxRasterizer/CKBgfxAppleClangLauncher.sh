compiler="$1"
shift

if [ -n "$CKRE_APPLE_CLANGXX" ]; then
    exec "$CKRE_APPLE_CLANGXX" "$@"
fi

exec /usr/bin/clang++ "$@"
