#include <gtest/gtest.h> 
#include <llle/concepts.hpp>

// A type that fits: 64 bytes, aligned to 64
struct alignas(64) GoodSlot {
    char data[64];
};

// A type that doesn't fit: too large
struct alignas(64) TooBig {
    char data[65];
};

struct BadAlignment {
    char data[32];
};

struct NonPolymorphic {
    void foo() {}
};

struct Polymorphic {
    virtual void foo() {}
};

struct POD {
    int x;
    float y;
};

class TypeWithDestructor {
    public:
    ~TypeWithDestructor() {}
};

TEST(FitsCacheLine, PassesForCorrectType) {
    EXPECT_TRUE(llle::FitsCacheLine<GoodSlot>);
}

TEST(FitsCacheLine, FailsForOversizedType) {
    EXPECT_FALSE(llle::FitsCacheLine<TooBig>);
}

TEST(FitsCacheLine, FailsForWrongAlignment) {
    EXPECT_FALSE(llle::FitsCacheLine<BadAlignment>);
}

TEST(IsNotVirtual, PassesForNonPolymorphicType) {
    EXPECT_TRUE(llle::IsNotVirtual<NonPolymorphic>);
}

TEST(IsNotVirtual, FailsForPolymorphicType) {
    EXPECT_FALSE(llle::IsNotVirtual<Polymorphic>);
}

TEST(IsPowerOfTwo, PassesForPowersOfTwo) {
    EXPECT_TRUE(llle::IsPowerOfTwo<1>);
    EXPECT_TRUE(llle::IsPowerOfTwo<2>);
    EXPECT_TRUE(llle::IsPowerOfTwo<4>);
    EXPECT_TRUE(llle::IsPowerOfTwo<8>);
    EXPECT_TRUE(llle::IsPowerOfTwo<16>);
    EXPECT_TRUE(llle::IsPowerOfTwo<32>);
    EXPECT_TRUE(llle::IsPowerOfTwo<64>);
}

TEST(IsPowerOfTwo, FailsForNonPowersOfTwo) {
    EXPECT_FALSE(llle::IsPowerOfTwo<0>);
    EXPECT_FALSE(llle::IsPowerOfTwo<3>);
    EXPECT_FALSE(llle::IsPowerOfTwo<5>);
    EXPECT_FALSE(llle::IsPowerOfTwo<6>);
    EXPECT_FALSE(llle::IsPowerOfTwo<7>);
    EXPECT_FALSE(llle::IsPowerOfTwo<9>);
    EXPECT_FALSE(llle::IsPowerOfTwo<10>);
}

TEST(IsTriviallyCopyable, PassesForTrivialForPOD) {
    EXPECT_TRUE(llle::IsTriviallyCopyable<POD>);
}

TEST(IsTriviallyCopyable, FailsForTypeWithDestructor) {
    EXPECT_FALSE(llle::IsTriviallyCopyable<TypeWithDestructor>);
}