@interface test
{
        int x;
}

+ do;
@end

@implementation test

+do
{
        doit();
}

@end

int main()
{
[test do];
}