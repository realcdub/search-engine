#include <stdio.h>
#include <curl/curl.h>
#include <gumbo.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <ctype.h>

#define BUFFER_MAX_AMOUNT_OF_LINKS 10000

typedef struct {
	char* response;
	size_t size;
} Chunk;

size_t write_data(char *buffer, size_t size, size_t nmemb, void *userdata) {
	Chunk* chunk = (Chunk*)userdata;
	size_t real_size = size * nmemb;

	char* response = realloc(chunk->response, chunk->size + real_size + 1);

	if (response == nullptr) {
		printf("Realloc Failed!");
		return 0;
	}

	chunk->response = response;
	memcpy(chunk->response + chunk->size, buffer, real_size);
	chunk->size += real_size;
	chunk->response[chunk->size] = '\0';

	return real_size;
}

void search_for_links(GumboNode* node, char** links, size_t *number_of_links) {
	if (node->type != GUMBO_NODE_ELEMENT) {
		return;
	}

	GumboAttribute* href;
	if (node->v.element.tag == GUMBO_TAG_A && (href = gumbo_get_attribute(&node->v.element.attributes, "href"))) {
		if (*number_of_links == BUFFER_MAX_AMOUNT_OF_LINKS) {
			return;
		}

		links[*number_of_links] = strdup(href->value);
		*number_of_links += 1;
	}

	GumboVector* children = &node->v.element.children;
	for (unsigned int i = 0; i < children->length; ++i) {
		search_for_links((GumboNode*)(children->data[i]), links, number_of_links);
	}
}

void normalize_text(char* buffer) {
	size_t buffer_length = strlen(buffer);

	for (int i = 0; i < buffer_length; ++i) {
		buffer[i] = tolower(buffer[i]);
	}
}

void handle_query(sqlite3 *db_handle, int return_code, char* query, char* error_message) {
	sqlite3_free(query);

	if (return_code != SQLITE_OK) {
		if (error_message != NULL) {
			printf("%s\n", error_message);
			sqlite3_free(error_message);
		}
		sqlite3_close(db_handle);
		return;
	}
}

int insert_document(sqlite3 *db_handle, const char* url, unsigned int document_length) {
	char* query = "INSERT INTO Documents (url, length) VALUES (?, ?) ON CONFLICT(url) DO UPDATE SET url=url RETURNING doc_id;";
	int document_id = -1;

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("DOCUMENT INSERT ERROR: %s\n", error_message);
	}

	sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, document_length);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		//if (sqlite3_column_type() == SQLITE_INTEGER) {
		document_id = sqlite3_column_int(stmt, 0);
		//}
	}

	sqlite3_finalize(stmt);

	return document_id;
};

int insert_term(sqlite3 *db_handle, const char* term) {
	char* query = sqlite3_mprintf("INSERT INTO Terms (term) VALUES (?) ON CONFLICT(term) DO UPDATE SET term=term RETURNING term_id;", term);

	int term_id = -1;

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, term, -1, SQLITE_STATIC);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			//if (sqlite3_column_type() == SQLITE_INTEGER) {
			term_id = sqlite3_column_int(stmt, 0);
			//}
		}
	}

	sqlite3_finalize(stmt);

	return term_id;
};

void insert_posting(sqlite3 *db_handle, unsigned int term_id, unsigned int document_id) {
	// UPSERT (Update or Insert)
	char* query = sqlite3_mprintf("INSERT INTO Postings (term_id, doc_id, frequency) VALUES (%d, %d, 1) ON CONFLICT (term_id, doc_id) DO UPDATE SET frequency = frequency + 1;", term_id, document_id);
	char *error_message;
	int return_code = sqlite3_exec(db_handle, query, nullptr, nullptr, &error_message);

	handle_query(db_handle, return_code, query, error_message);
};

void process_text(sqlite3* db_handle, GumboNode* node, unsigned int document_id) {
	const char* delimeter = " \t\n\r.,!?;:()[]{}'\"-";

	if (node->type == GUMBO_NODE_TEXT) {
		const char* extracted_text = node->v.text.text;
		char* normalized_text = strdup(extracted_text);
		normalize_text(normalized_text);

		char* token = strtok(normalized_text, delimeter);
		while (token != nullptr) {
			printf("Processing token: %s\n", token);
			int term_id = insert_term(db_handle, token);
			insert_posting(db_handle, term_id, document_id);

			token = strtok(nullptr, delimeter);
		}

		free(normalized_text);
	}

	if (node->type != GUMBO_NODE_ELEMENT || 
			node->v.element.tag == GUMBO_TAG_SCRIPT || 
			node->v.element.tag == GUMBO_TAG_STYLE) 
	{
		return;
	}
 
	
	GumboVector* children = &node->v.element.children;
	for (unsigned int i = 0; i < children->length; ++i) {
		process_text(db_handle, (GumboNode*)(children->data[i]), document_id);
	}
}

int main(int argc, char** argv) {
	curl_global_init(CURL_GLOBAL_ALL);
	//curl_version_info_data * version_info = curl_version_info(CURLVERSION_NOW);

	//std::cout << version_info->ssl_version << std::endl;
	
	CURL *curl_handle = curl_easy_init();

	const char* starting_url = "https://en.wikipedia.org/";
	const char* user_agent = "cdub is just programming";
	const char* db_name = "index.db";

	sqlite3* db_handle; 
	sqlite3_open(db_name, &db_handle);

	if (curl_handle) {
		curl_easy_setopt(curl_handle, CURLOPT_URL, starting_url);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, user_agent);
		curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);

		Chunk chunk = {0};
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk);

		CURLcode success = curl_easy_perform(curl_handle);

		if (success == CURLE_OK) {
			GumboOutput* output = gumbo_parse(chunk.response);

			char *links[BUFFER_MAX_AMOUNT_OF_LINKS] = {NULL};
			size_t number_of_links = 0;

			int document_id = insert_document(db_handle, starting_url, (unsigned int)chunk.size);
			process_text(db_handle, output->root, document_id);

			search_for_links(output->root, links, &number_of_links);	
			gumbo_destroy_output(&kGumboDefaultOptions, output);

			for (int i = 0; i < BUFFER_MAX_AMOUNT_OF_LINKS; ++i) {
				if (links[i] == NULL) {
					continue;
				}

				memset(chunk.response, 0, chunk.size);
				chunk.size = 0;

				char* link = links[i];
				curl_easy_setopt(curl_handle, CURLOPT_URL, link);
				success = curl_easy_perform(curl_handle);

				if (success == CURLE_OK) {
					output = gumbo_parse(chunk.response);

					int document_id = insert_document(db_handle, link, (unsigned int)chunk.size);
					process_text(db_handle, output->root, document_id);

					gumbo_destroy_output(&kGumboDefaultOptions, output);
				} else {
					printf("%s: Request Failed!\n", link);
				}

				free(link);
			}

			free(chunk.response);
		} else {
			printf("Request Failed!\n");
		}
		
		curl_easy_cleanup(curl_handle);
	}

	sqlite3_close(db_handle);

	return 0;
}
