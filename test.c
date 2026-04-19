#include <stdio.h>

int main() {
	int a;
	long long int b = 28;
	printf("Enter a number: ");
	scanf("%d", &a);
	printf("Your number plus five is: %d\n", a + 5);
	printf("stored long long value is: %lld\n", b);
}