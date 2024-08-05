# webserv




#### Info

``` bash 
# This Make command will launch a container in localhost and we can test the proxypassing to this container from our reverse proxy
make proxy-cgi-test

```

### Check the tests/proxy-cgi-test.conf 

+ Change the **server_name** to "localhost" if accessing trough host taht you dont have root or sudo priviledges therefore use virtualmachine to run the server so you can name your own domain to /etc/hosts and map it to localhost"
```bash
# In vm 
echo "mydomainname.com >> /etc/hosts"
```

#### Access the prot that rev proxy is listening map it to non standard over 1024

mydomain.com:4242



#### in browser use the fn + f12 and network tab to see if the requests go trough and with what HTTP status codes are the requests returning 200 = OK

### Configuration file setup

+ the file extension must be .conf
+ directives must be followed by ';'
+ unclosed braces '{}' are not allowed
+ Currently, the only accepted format for contexts is
  	```
	server {
		...
	}

	as opposed to for example

	server
	{
		...
	}
	```

	This latter format will throw an error.

+ trailing whitespaces (after semicolons, braces, or on empty lines) are not allowed

+ Comments can be created by typing '#' -> can also be used inline, in which case, everything after it is ignored (there is no option to close it off)

+ port's ("listen" directive) value must be a number 0-65535. Setting it to under 1024 will give a warning

+ server_name field is optional. Several strings can be specified, which have to be separated by spaces

+ client_max_body_size can be set in kilobytes or megabytes (for example 1K or 5M). The field can be omitted, and then the default will be in effect, which is 1m. If it is set to "0" (not 0K or 0M though), body size will be unlimited (though we might want to limit the max max_body_size...? For now, I'll just limit it at LONG_MAX :D)

+ setting up default error pages is optional, but if done, must be done in the server context in the following format:

	```
	error_page <error_code1> <error_code2> <error_code_n> <error_page_address>
	```

	(whitespaces between the elements are not counted)

	The error page address can be a URI or a URL at the moment
	(thus, if the error page address does not contain at least one '/', an error will be thrown during the parsing)



Proxy_pass passes the requests to other server off loads the work from our server and the server its passed to in this examples a docker containers have their own CGI to handle the code execution and generate the reponse for the user. 

