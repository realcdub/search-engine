#include <stdio.h>
#include <curl/curl.h>
#include <gumbo.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define BUFFER_MAX_AMOUNT_OF_LINKS 50

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

void extract_text(GumboNode* node) {
	if (node->type == GUMBO_NODE_TEXT) {
		const char* text = node->v.text.text;
		printf("%s\n", text);
	}

	if (node->type != GUMBO_NODE_ELEMENT || 
			node->type == GUMBO_TAG_SCRIPT || 
			node->type == GUMBO_TAG_STYLE) 
	{
		return;
	}
 
	
	GumboVector* children = &node->v.element.children;
	for (unsigned int i = 0; i < children->length; ++i) {
		extract_text((GumboNode*)(children->data[i]));
	}
}

void handle_query(sqlite3 *db_handle, int return_code, char* query, char* error_message) {
	sqlite3_free(query);

	if (return_code == SQLITE_ABORT) {
		if (error_message != NULL) {
			printf("%s\n", error_message);
			sqlite3_free(error_message);
		}
		sqlite3_close(db_handle);
		return;
	}
}

void insert_document(sqlite3 *db_handle, const char* url, unsigned int document_length) {
	// Be careful here (%Q, %d), possible switch to prepare statements if SQL injections is an issue
	char* query = sqlite3_mprintf("INSERT INTO Documents (url, length) VALUES (%Q, %d);", url, document_length);
	char *error_message;
	int return_code = sqlite3_exec(db_handle, query, nullptr, nullptr, &error_message);	

	handle_query(db_handle, return_code, query, error_message);
};

void insert_term(sqlite3 *db_handle, const char* term) {
	char* query = sqlite3_mprintf("INSERT INTO Terms (term) VALUES (%Q);", term);
	char *error_message;
	int return_code = sqlite3_exec(db_handle, query, nullptr, nullptr, &error_message);

	handle_query(db_handle, return_code, query, error_message);
};

void insert_posting(sqlite3 *db_handle, unsigned int term_id, unsigned int document_id, unsigned int frequency) {
	char* query = sqlite3_mprintf("INSERT INTO Postings (term_id, doc_id, frequency) VALUES (%d, %d, %d);", term_id, document_id, frequency);
	char *error_message;
	int return_code = sqlite3_exec(db_handle, query, nullptr, nullptr, &error_message);

	handle_query(db_handle, return_code, query, error_message);
};

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

			insert_document(db_handle, starting_url, (unsigned int)chunk.size);

			extract_text(output->root);
			//search_for_links(output->root, links, &number_of_links);	
			gumbo_destroy_output(&kGumboDefaultOptions, output);

			/*

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
					//printf("%s   %zu\n", link, chunk.size);
					insert_document(db_handle, link, (unsigned int)chunk.size);
					output = gumbo_parse(chunk.response);
					gumbo_destroy_output(&kGumboDefaultOptions, output);
				} else {
					printf("%s: Request Failed!\n", link);
				}

				free(link);
			}

			free(chunk.response);

			*/
		} else {
			printf("Request Failed!\n");
		}
		
		curl_easy_cleanup(curl_handle);
	}

	sqlite3_close(db_handle);

	return 0;
}
