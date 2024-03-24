"""Hooks for mkdocs.

See https://www.mkdocs.org/user-guide/configuration/#hooks.
"""


def on_page_markdown(markdown, **kwargs):
    del kwargs

    # The markdown files in the root of the repo refer to each other by name so that
    # links between them work on the GitHub code view.
    # However, when we bring these pages into the mkdocs site, we lower-case the files
    # so the URLs look better. To make the cross-links between the pages work, do light
    # surgery on them:
    return markdown.replace("ARCHITECTURE.md", "architecture.md").replace(
        "CONTRIBUTING.md", "contributing.md"
    )
