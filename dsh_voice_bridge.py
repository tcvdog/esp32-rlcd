#!/usr/bin/env python3
"""Inject a transcribed voice command into the most recent DSH web session."""
import os, pathlib, sys
from playwright.sync_api import sync_playwright

ROOT = pathlib.Path(__file__).resolve().parent
TOKEN_FILE = pathlib.Path.home() / '.dsh' / 'web-ui-url.txt'
TITLE = os.environ.get('DSH_VOICE_SESSION_TITLE', '你是一个擅长')

def main(text):
    url = TOKEN_FILE.read_text().strip()
    with sync_playwright() as p:
        b = p.chromium.launch(headless=True, executable_path='/usr/bin/google-chrome',
                              args=['--no-sandbox', '--disable-dev-shm-usage'])
        page = b.new_page(viewport={'width': 1200, 'height': 850})
        page.goto(url, wait_until='domcontentloaded', timeout=30000)
        page.wait_for_timeout(3500)
        # The most recent session is the first sidebar entry; prefer the known title.
        loc = page.get_by_text(TITLE, exact=False)
        if loc.count() == 0:
            loc = page.get_by_text('你是一个', exact=False)
        if loc.count() > 0:
            loc.first.click()
            page.wait_for_timeout(2500)
        box = page.locator('[contenteditable="true"]').first
        box.click()
        box.fill(text)
        page.wait_for_timeout(300)
        page.keyboard.press('Enter')
        page.wait_for_timeout(2500)
        page.screenshot(path='/tmp/dsh_voice_injected.png')
        b.close()
    print('injected:', text)

if __name__ == '__main__':
    if len(sys.argv) < 2 or not sys.argv[1].strip():
        raise SystemExit('empty command')
    main(sys.argv[1])
