#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>

typedef struct {
	char* url;
	double score;
} SearchResult;

double get_bm25_score(const char* db_name, int doc_id, int term_id);

static int get_total_number_of_documents(const char* db_name) {
	sqlite3* db_handle;
	sqlite3_open(db_name, &db_handle);

	char* query = "SELECT COUNT() FROM Documents;";
	int total = -1;

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("GET TOTAL DOCUMENTS ERROR: %s\n", error_message);
	}

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		total = sqlite3_column_int(stmt, 0);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db_handle);

	return total;
}

static int get_average_document_length(const char* db_name, int term_id) {
	sqlite3* db_handle;
	sqlite3_open(db_name, &db_handle);

	char* query = "SELECT AVG(length) FROM Postings INNER JOIN Documents ON Postings.doc_id=Documents.doc_id WHERE term_id=?;";
	int average = -1;

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("GET AVERAGE LENGTH ERROR: %s\n", error_message);
	}

	sqlite3_bind_int(stmt, 1, term_id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		//if (sqlite3_column_type() == SQLITE_INTEGER) {
		average = sqlite3_column_int(stmt, 0);
		//}
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db_handle);

	return average;
}

static int get_total_documents_with_term(const char* db_name, int term_id) {
	char* query = "SELECT COUNT() FROM Postings WHERE term_id=?;";
	int document_id = -1;

	sqlite3* db_handle;
	sqlite3_open(db_name, &db_handle);

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("GET TOTAL DOCUMENTS WITH TERM ERROR: %s\n", error_message);
	}

	sqlite3_bind_int(stmt, 1, term_id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		//if (sqlite3_column_type() == SQLITE_INTEGER) {
		document_id = sqlite3_column_int(stmt, 0);
		//}
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db_handle);

	return document_id;
}

int get_term_frequency(const char* db_name, int term_id, int doc_id) {
	char* query = "SELECT frequency FROM Postings WHERE term_id=? AND doc_id=?";
	int frequency = -1;

	sqlite3* db_handle;
	sqlite3_open(db_name, &db_handle);

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("GET TERM FREQUENCY ERROR: %s\n", error_message);
	}

	sqlite3_bind_int(stmt, 1, term_id);
	sqlite3_bind_int(stmt, 2, doc_id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		frequency = sqlite3_column_int(stmt, 0);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db_handle);

	return frequency;
}

static int get_document_length(const char* db_name, int doc_id) {
	char* query = "SELECT length FROM Documents WHERE doc_id=?";
	int length = -1;

	sqlite3* db_handle;
	sqlite3_open(db_name, &db_handle);

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("GET DOCUMENT LENGTH ERROR: %s\n", error_message);
	}

	sqlite3_bind_int(stmt, 1, doc_id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		length = sqlite3_column_int(stmt, 0);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db_handle);

	return length;
}

static double get_inverse_document_frequency(int total_docs, int total_docs_with_keyword) {
	return log((total_docs - total_docs_with_keyword + 0.5) / (total_docs_with_keyword + 0.5) + 1);
}

// Pass in your array, the size (X), the new URL, and its score
void update_top_results(SearchResult *top_x, int max_results, char* new_url, double new_score) {
    
    // 1. Instant Rejection: 
    // If the new score is worse than the lowest score in our list (the last slot), 
    // we throw it away immediately and do nothing.
    if (new_score <= top_x[max_results - 1].score) {
        return; 
    }

    // 2. Find its spot and shift the losers down
    // We start from the second-to-last item and work our way UP the rankings
    int i;
    for (i = max_results - 2; i >= 0; i--) {
        if (new_score > top_x[i].score) {
            // The new score beat this guy, so bump this guy down one slot
            top_x[i + 1] = top_x[i];
        } else {
            // We found a score higher than ours, so we stop shifting
            break;
        }
    }

    // 3. Insert the new winner into the empty slot we just created
    top_x[i + 1].url = new_url;
    top_x[i + 1].score = new_score;
}

void get_scores_for_urls(const char* db_name, char** terms, int number_of_terms, SearchResult* results, int max_results) {
	char query[1000];
	strcpy(query, "SELECT Postings.doc_id, Postings.term_id, Documents.url FROM Postings JOIN Documents ON Postings.doc_id=Documents.doc_id JOIN Terms ON Postings.term_id=Terms.term_id WHERE Terms.term IN (");

	for (int i = 0; i < number_of_terms; ++i) {
		char* term = terms[i];
		strcat(query, "?");
		if (i < number_of_terms - 1) strcat(query, ", ");
	}

	strcat(query, ") ORDER BY Postings.doc_id;");

	sqlite3* db_handle;
	sqlite3_open(db_name, &db_handle);

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("GET SCORES FOR URLS ERROR: %s\n", error_message);
	}


	for (int i = 0; i < number_of_terms; ++i) {
		char* term = terms[i];
		sqlite3_bind_text(stmt, i + 1, term, -1, SQLITE_STATIC);
	}

	int previous_doc_id = -1;
	double total_score = 0;

	char previous_url[200];

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int doc_id = sqlite3_column_int(stmt, 0);
		int term_id = sqlite3_column_int(stmt, 1);
		
		if (previous_doc_id != doc_id) {
			if (previous_doc_id != -1) {
				update_top_results(results, max_results, strdup(previous_url), total_score);
				total_score = 0;
			}
			previous_doc_id = doc_id;
		}

		memset(previous_url, 0, 200);
		strcpy(previous_url, sqlite3_column_text(stmt, 2));

		total_score += get_bm25_score(db_name, doc_id, term_id);
	}
	
	if (previous_doc_id != -1) {
		update_top_results(results, max_results, strdup(previous_url), total_score);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db_handle);
};

int get_term_id(const char* db_name, const char* term) {
	char* query = "SELECT term_id FROM Terms WHERE term=?";
	int term_id = -1;

	sqlite3* db_handle;
	sqlite3_open(db_name, &db_handle);

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_v2(db_handle, query, -1, &stmt, NULL) != SQLITE_OK) {
		const char* error_message = sqlite3_errmsg(db_handle);
		printf("GET TERM ID ERROR: %s\n", error_message);
	}

	sqlite3_bind_text(stmt, 1, term, -1, SQLITE_STATIC);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		term_id = sqlite3_column_int(stmt, 0);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db_handle);

	return term_id;
};

double get_bm25_score(const char* db_name, int doc_id, int term_id) {
	int total_docs = get_total_number_of_documents(db_name);
	///printf("Total Docs: %d\n", total_docs);

	int total_docs_with_keyword = get_total_documents_with_term(db_name, term_id);
	//printf("Total Docs With Keyword: %d\n", total_docs_with_keyword);

	double idf = get_inverse_document_frequency(total_docs, total_docs_with_keyword);
	//printf("IDF: %lf\n", idf);

	int term_frequency = get_term_frequency(db_name, term_id, doc_id);
	//printf("Term Frequency: %d\n", term_frequency);

	double average_doc_length_with_term = get_average_document_length(db_name, term_id);
	//printf("Average Doc Length With Term: %lf\n", average_doc_length_with_term);

	int k1 = 1.6;
	int b = 0.75;

	return idf * term_frequency * (k1 + 1) / (term_frequency + (k1 * (1 - b + b * total_docs / average_doc_length_with_term)));
}

int main(int argc, char* argv[]) {
	if (argc == 1) {
		printf("Not enough arguments!\n");
		return 1;
	}

	int number_of_terms = argc - 1;
	char** terms = argv + 1;

	int max_results = 10;

	SearchResult top_results[max_results] = {};

	for (int i = 1; i < argc; ++i) {
		char* term = argv[i];
		terms[i] = term;
	}

	get_scores_for_urls("index.db", terms, number_of_terms, top_results, max_results);

	int ranking = 1;
	for (int i = 0; i < max_results; ++i) {
		SearchResult result = top_results[i];
		if (result.url == nullptr) {
			continue;
		}

		printf("%d url: %s score: %lf\n", ranking, result.url, result.score);
		ranking += 1;
	}

	return 0;
}
