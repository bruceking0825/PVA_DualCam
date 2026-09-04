
// 打印一行字符串
#include <QDebug>
#include <QString>
#include <iostream>   // std::cout
#include <cstring>    // std::strlen

struct Person{
    std::string name;
    int age;
    double height;
    double weight;
    
};
template <typename T>
double add10(T src)
{
    return src+10.5;
}

int main()
{
    // QString text = "hello";

    // qDebug() << sizeof(text); // QString对象自身的大小
    // qDebug() << text.size();  // 5

    // char second[] = "hello";

    // std::cout << type(second) << '\n';

    double Person::* member = &Person::height;

    Person Bruce{"Bruce", 38, 175.5, 85.5};
    Person Celina{"Celina", 13, 161.5, 30.5};
    std::cout << add10(Bruce.*member) << '\n';

    member = &Person::weight;
    std::cout << Bruce.*member << '\n';
    

}