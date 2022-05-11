#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(){
	char *a;
	char *save_ptr;
	char *token[10];
	int i;
	char s[] = "hello my name is";
	token[0] = strtok_r(s, " ", &save_ptr);
	printf("%s\n", token[0]);
	for (i=0; i<3; i++){
		
		token[i+1] = strtok_r(NULL, " ", &save_ptr);
		printf("%s\n", token[i+1]);
	}
	
	
	
	//printf("%s\n",s);
	return 0;

}
