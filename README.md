# C Web Crawler & Search Engine

A search engine written entirely in C. This project demonstrates systems-level programming, manual memory management, and information retrieval concepts by fetching live web pages, parsing HTML DOM structures, and ranking search results using the BM25 mathematical algorithm.

## Features

*   **Automated Web Crawler:** Utilizes `libcurl` to handle HTTP network requests, traverse hyperlinks, and dynamically allocate memory for incoming data chunks.
*   **HTML DOM Parsing:** Integrates Google's `Gumbo` parsing library to traverse HTML trees, extracting raw text and embedded `href` links while ignoring scripts and styles.
*   **Relational Data Storage:** Tokenizes normalized text in a SQLite database schema (Documents, Terms, and Postings tables).
*   **BM25 Search Ranking:** Queries the SQLite database to calculate a score, which directly influences the ranking of search results.

## Technical Stack

*   **Language:** C (Standard C2X)
*   **Network:** `libcurl`
*   **HTML Parsing:** `gumbo-parser`
*   **Database:** `sqlite3`

## System Architecture

The project is split into two distinct, decoupled executables:

1.  **The Crawler (`crawler.c`):** Initializes a connection starting from a seed URL (Wikipedia). It fetches the HTML, parses the DOM for new links, tokenizes all valid text strings, and performs SQL operations to store URLs that were found along with information such as document length.
2.  **The Search Engine (`search.c`):** A command-line tool that accepts user query terms. It queries the `index.db` database using SQL operations to find matching documents, calculates the BM25 score for each document-term pair, and sorts the top 10 results using an in-place array shifting algorithm.

## Installation & Setup

### Prerequisites
Ensure you have the required dependencies and development libraries installed on your Linux environment:

```bash
sudo apt install libcurl4-gnutls-dev sqlite3 libsqlite3-dev
```

### 1. Run the crawler to fetch pages and build the index
```bash
./crawler
```
### 2. Run the search engine with any number of query terms
```bash
./search [QUERY_TERM_1] [QUERY_TERM_2] ... [QUERY_TERM_N]
```
