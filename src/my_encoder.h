

#pragma once

#include <memory>

namespace my_encoder
{

class MyEncoderImpl;
class MyEncoder {

public:
    MyEncoder();

private:
    std::unique_ptr<MyEncoderImpl> impl;
};

}