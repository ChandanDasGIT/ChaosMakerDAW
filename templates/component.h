#pragma once

// One line: what this part does. No Qt in backend/.
class Component {
public:
    int value() const { return m_value; }
    void setValue(int value);

private:
    int m_value = 0;
};
