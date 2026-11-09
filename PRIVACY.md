# Privacy

Version: 1.0
Effective: 09 November 2026
Owner: Dr Sohil Momin — sohilmomin2000@gmail.com

## What this repository is

A browser engine — a C++ project derived from Ladybird/SerenityOS. It is a rendering and networking engine, not
a privacy product, and it has no account system, no telemetry and no analytics.

## What the repository contains

Engine source, tests and test fixtures. Some fixtures contain `fetch` calls and cookie cases; they are inputs
to the test suite, not behaviour you run.

## What the engine does with your data

A browser engine fetches what you ask it to fetch. It sends the requests a browser sends — the URL, the
headers, your cookies for that site — to the sites you visit. It does not add analytics, does not phone home,
and does not send anything to the author.

Whatever a page you load does with your data is that page's business, under that page's privacy policy.

## Honest limits

This is an experimental engine. It is not hardened, has not been audited, and should not be used to browse
anything you would not browse in a fresh, unpatched browser.

Contact: Dr Sohil Momin — sohilmomin2000@gmail.com
