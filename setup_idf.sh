#!/usr/bin/env bash
# Source this file from a bash shell:
#   source esp32-rlcd/setup_idf.sh
#
# ESP-IDF was installed with the system Python 3.12, so make sure an unrelated
# Python virtualenv (for example the Hermes agent venv) does not shadow it.
export IDF_PATH="$HOME/esp/esp-idf"
if [ ! -f "$IDF_PATH/export.sh" ]; then
  echo "ESP-IDF is not installed at $IDF_PATH" >&2
  return 1 2>/dev/null || exit 1
fi
if [ -n "$VIRTUAL_ENV" ] && [ "$VIRTUAL_ENV" != "$HOME/.espressif/python_env/idf5.4_py3.12_env" ]; then
  PATH=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v "^$VIRTUAL_ENV" | paste -sd:)
  unset VIRTUAL_ENV
fi
export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"
. "$IDF_PATH/export.sh"
# cmake/ninja were installed into the IDF Python env (no sudo needed).
export PATH="$HOME/.espressif/python_env/idf5.4_py3.12_env/bin:$PATH"
