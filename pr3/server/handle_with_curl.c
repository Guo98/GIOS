

#include "gfserver.h"
#include "proxy-student.h"

#define BUFSIZE (630)

/*
 * Replace with an implementation of handle_with_curl and any other
 * functions you may need.
 */

typedef struct curl_resp {
	char *response;
	size_t size;
} curl_resp;

// similar callback to the example posted on https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
size_t write_callback(void *data, size_t size, size_t nmemb, void* arg) {
	size_t actual_size = size * nmemb;
	struct curl_resp *curl_result = (struct curl_resp *)arg;

	curl_result->response = realloc(curl_result->response, curl_result->size + actual_size + 1);
	memcpy(&(curl_result->response[curl_result->size]), data, actual_size);
	curl_result->size += actual_size;
	curl_result->response[curl_result->size] = 0;

	return actual_size;
}

ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void* arg){
	(void) ctx;
	(void) path;
	(void) arg;
	char reqUrl[BUFSIZE];
	snprintf(reqUrl, BUFSIZE, "%s%s",(char *) arg, path);

	CURL *curl = curl_easy_init();
	CURLcode res;
	struct curl_resp curl_result = {
		.response = NULL,
		.size = 0
	};
	long response_code;
	if(curl) {
			// setting options
			curl_easy_setopt(curl, CURLOPT_URL, reqUrl);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&curl_result);

			res = curl_easy_perform(curl);
			if(res != CURLE_OK) {
				fprintf(stderr, "Error in curl easy perform: %s\n", curl_easy_strerror(res));
				return -1;
			}

			CURLcode resp_code_res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
			if(resp_code_res != CURLE_OK) {
				fprintf(stderr, "Error in curl easy getinfo in getting response code: %s\n", curl_easy_strerror(resp_code_res));
				return -1;
			}
			// CURLcode length_res;
			// length_res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &download_content_length);
			// if(length_res != CURLE_OK) {
			// 	fprintf(stderr, "Error in curl easy getinfo: %s\n", curl_easy_strerror(length_res));
			// 	return -1;
			// }

			printf("Length form struct: %zu\n", curl_result.size);
			curl_easy_cleanup(curl);
	}

	if(response_code > 500) {
		return gfs_sendheader(ctx, GF_ERROR, 0);
	} else if(response_code >= 400 && response_code < 500) {
		return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
	} else {
		gfs_sendheader(ctx, GF_OK, curl_result.size);
	}

	int sent_bytes = 0;
	int sending_bytes;

	while(sent_bytes < curl_result.size) {
		sending_bytes = gfs_send(ctx, curl_result.response, curl_result.size);
		if(sending_bytes < 0) {
			fprintf(stderr, "Error in sending data.\n");
			return -1;
		}
		sent_bytes += sending_bytes;
	}
	/* not implemented */
	// errno = ENOSYS;
	free(curl_result.response);
	return sent_bytes;
}

/*
 * We provide a dummy version of handle_with_file that invokes handle_with_curl
 * as a convenience for linking.  We recommend you simply modify the proxy to
 * call handle_with_curl directly.
 */
ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void* arg){
	return handle_with_curl(ctx, path, arg);
}
