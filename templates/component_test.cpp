#include "testkit.h"

#include "component/component.h"

TEST(component, remembers_its_value)
{
    Component component;
    component.setValue(3);
    CHECK(component.value() == 3);
}
