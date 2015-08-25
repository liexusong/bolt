#include "bolt.h"

char bolt_error_400_page[] =
"<html>" BOLT_CRLF
"<head><title>400 Bad Request</title></head>" BOLT_CRLF
"<body bgcolor=\"white\">" BOLT_CRLF
"<center><h1>400 Bad Request</h1></center>" BOLT_CRLF
;

char bolt_error_404_page[] =
"<html>" BOLT_CRLF
"<head><title>404 Not Found</title></head>" BOLT_CRLF
"<body bgcolor=\"white\">" BOLT_CRLF
"<center><h1>404 Not Found</h1></center>" BOLT_CRLF
;

char bolt_error_500_page[] =
"<html>" BOLT_CRLF
"<head><title>500 Internal Server Error</title></head>" BOLT_CRLF
"<body bgcolor=\"white\">" BOLT_CRLF
"<center><h1>500 Internal Server Error</h1></center>" BOLT_CRLF
;
