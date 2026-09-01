
// 打印一行字符串
#include <QDebug>
#include <QString>
#include <iostream>   // std::cout
#include <cstring>    // std::strlen
int main()
{
    QString text = "hello";

    qDebug() << sizeof(text); // QString对象自身的大小
    qDebug() << text.size();  // 5

    char second[] = "hello";

    // std::cout << type(second) << '\n';

}