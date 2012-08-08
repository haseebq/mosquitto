/*
Copyright (c) 2012 Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/


#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/buffer.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#  include <process.h>
#else
#  include <unistd.h>
#endif

#define MAX_BUFFER_LEN 1024
#define SALT_LEN 12

int base64_encode(unsigned char *in, unsigned int in_len, char **encoded)
{
	BIO *bmem, *b64;
	BUF_MEM *bptr;

	b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	bmem = BIO_new(BIO_s_mem());
	b64 = BIO_push(b64, bmem);
	BIO_write(b64, in, in_len);
	if(BIO_flush(b64) != 1){
		BIO_free_all(b64);
		return 1;
	}
	BIO_get_mem_ptr(b64, &bptr);
	*encoded = calloc(bptr->length+1, 1);
	if(!(*encoded)){
		BIO_free_all(b64);
		return 1;
	}
	memcpy(*encoded, bptr->data, bptr->length);
	(*encoded)[bptr->length] = '\0';
	BIO_free_all(b64);

	return 0;
}


void print_usage(void)
{
	printf("mosquitto_passwd is a tool for managing password files for mosquitto.\n\n");
	printf("Usage: mosquitto_passwd [-c | -D] passwordfile username\n");
	printf(" -c : create a new password file. This will overwrite existing files.\n");
	printf(" -D : delete the username rather than adding/updating its password.\n");
	printf("\nSee http://mosquitto.org/ for more information.\n\n");
}

int output_new_password(FILE *fptr, const char *username, const char *password)
{
	int rc;
	unsigned char salt[SALT_LEN];
	char *salt64 = NULL, *hash64 = NULL;
	unsigned char hash[EVP_MAX_MD_SIZE];
	unsigned int hash_len;
	const EVP_MD *digest;
	EVP_MD_CTX context;
	char *pass_salt;
	int pass_salt_len;

	rc = RAND_bytes(salt, SALT_LEN);
	if(!rc){
		fprintf(stderr, "Error: Insufficient entropy available to perform password generation.\n");
		return 1;
	}

	rc = base64_encode(salt, SALT_LEN, &salt64);
	if(rc){
		if(salt64) free(salt64);
		fprintf(stderr, "Error: Unable to encode salt.\n");
		return 1;
	}


	digest = EVP_get_digestbyname("sha512");
	if(!digest){
		fprintf(stderr, "Error: Unable to create openssl digest.\n");
		return 1;
	}

	pass_salt_len = strlen(password) + SALT_LEN;
	pass_salt = malloc(pass_salt_len);
	if(!pass_salt){
		if(salt64) free(salt64);
		if(hash64) free(hash64);
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	memcpy(pass_salt, password, strlen(password));
	memcpy(pass_salt+strlen(password), salt, SALT_LEN);
	EVP_MD_CTX_init(&context);
	EVP_DigestInit_ex(&context, digest, NULL);
	EVP_DigestUpdate(&context, pass_salt, pass_salt_len);
	EVP_DigestFinal_ex(&context, hash, &hash_len);
	EVP_MD_CTX_cleanup(&context);
	free(pass_salt);

	rc = base64_encode(hash, hash_len, &hash64);
	if(rc){
		if(salt64) free(salt64);
		if(hash64) free(hash64);
		fprintf(stderr, "Error: Unable to encode hash.\n");
		return 1;
	}

	fprintf(fptr, "%s:$6$%s$%s\n", username, salt64, hash64);
	free(salt64);
	free(hash64);

	return 0;
}

int delete_pwuser(FILE *fptr, FILE *fnew, const char *username)
{
	char buf[MAX_BUFFER_LEN];
	char lbuf[MAX_BUFFER_LEN], *token;
	bool found = false;

	while(!feof(fptr) && fgets(buf, MAX_BUFFER_LEN, fptr)){
		memcpy(lbuf, buf, MAX_BUFFER_LEN);
		token = strtok(lbuf, ":");
		if(strcmp(username, token)){
			fprintf(fnew, "%s", buf);
		}else{
			found = true;
		}
	}
	if(!found){
		fprintf(stderr, "Warning: User %s not found in password file.\n", username);
	}
	return 0;
}

int update_pwuser(FILE *fptr, FILE *fnew, const char *username, const char *password)
{
	char buf[MAX_BUFFER_LEN];
	char lbuf[MAX_BUFFER_LEN], *token;
	bool found = false;
	int rc = 1;

	while(!feof(fptr) && fgets(buf, MAX_BUFFER_LEN, fptr)){
		memcpy(lbuf, buf, MAX_BUFFER_LEN);
		token = strtok(lbuf, ":");
		if(strcmp(username, token)){
			fprintf(fnew, "%s", buf);
		}else{
			rc = output_new_password(fnew, username, password);
			found = true;
		}
	}
	if(found){
		return rc;
	}else{
		return output_new_password(fnew, username, password);
	}
}

int get_password(char *password, int len)
{
	char pw1[MAX_BUFFER_LEN], pw2[MAX_BUFFER_LEN];

	printf("Password: ");
	/* FIXME - shouldn't echo password to screen. */
	if(!fgets(pw1, MAX_BUFFER_LEN, stdin)){
		fprintf(stderr, "Error: Empty password.\n");
		return 1;
	}else{
		while(pw1[strlen(pw1)-1] == 10 || pw1[strlen(pw1)-1] == 13){
			pw1[strlen(pw1)-1] = 0;
		}
		if(strlen(pw1) == 0){
			fprintf(stderr, "Error: Empty password.\n");
			return 1;
		}
	}
	printf("Reenter password: ");
	/* FIXME - shouldn't echo password to screen. */
	if(!fgets(pw2, MAX_BUFFER_LEN, stdin)){
		fprintf(stderr, "Error: Empty password.\n");
		return 1;
	}else{
		while(pw2[strlen(pw2)-1] == 10 || pw2[strlen(pw2)-1] == 13){
			pw2[strlen(pw2)-1] = 0;
		}
		if(strlen(pw2) == 0){
			fprintf(stderr, "Error: Empty password.\n");
			return 1;
		}
	}

	if(strcmp(pw1, pw2)){
		fprintf(stderr, "Error: Passwords do not match.\n");
		return 1;
	}

	strncpy(password, pw1, len);
	return 0;
}

int main(int argc, char *argv[])
{
	char *password_file = NULL;
	char temp_file[100];
	char *username = NULL;
	bool create_new = false;
	bool delete_user = false;
	FILE *fptr, *fnew;
	char password[MAX_BUFFER_LEN];
	int rc;

	OpenSSL_add_all_digests();

	if(argc == 4){
		if(!strcmp(argv[1], "-c")){
			create_new = true;
		}else if(!strcmp(argv[1], "-D")){
			delete_user = true;
		}
		password_file = argv[2];
		username = argv[3];
	}else if(argc == 3){
		password_file = argv[1];
		username = argv[2];
	}else{
		print_usage();
		return 1;
	}

	if(create_new){
		rc = get_password(password, 1024);
		if(rc) return rc;
		fptr = fopen(password_file, "wt");
		if(!fptr){
			fprintf(stderr, "Error: Unable to open file %s for writing.\n", password_file);
			return 1;
		}
		rc = output_new_password(fptr, username, password);
		fclose(fptr);
	}else{
		snprintf(temp_file, 100, "mosquitto_passwd_tmp.%d", getpid());
		fptr = fopen(password_file, "rt");
		if(!fptr){
			fprintf(stderr, "Error: Unable to open file %s for reading.\n", password_file);
			return 1;
		}
		fnew = fopen(temp_file, "wt");
		if(!fnew){
			fprintf(stderr, "Error: Unable to open file %s for writing.\n", temp_file);
			fclose(fptr);
			return 1;
		}
		if(delete_user){
			rc = delete_pwuser(fptr, fnew, username);
		}else{
			rc = get_password(password, 1024);
			if(rc) return rc;
			/* Update */
			rc = update_pwuser(fptr, fnew, username, password);
		}
		fclose(fptr);
		fclose(fnew);
		if(!rc){
			rename(temp_file, password_file);
		}else{
			fprintf(stderr, "Error occurred, not updating password file.\n");
		}
	}

	return 0;
}
