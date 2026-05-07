extern int ProjectRefConfigCollisionLibAValue();
extern int ProjectRefConfigCollisionLibBValue();

int main()
{
    return ProjectRefConfigCollisionLibAValue() + ProjectRefConfigCollisionLibBValue() == 3 ? 0 : 1;
}