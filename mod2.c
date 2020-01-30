int mod2__var = 1010;

char *
mod2__get_name(void)
{
    int i;

    printf ("Testing Module 2...");
    for (i = 0; i < 10; i++)
	printf("%i ", i);
    printf ("\n");
    return "Module 2 is ok.";
}

int
mod2__init(void)
{
    printf("INITIALIZE MOD2\n");
    return 0;
}
