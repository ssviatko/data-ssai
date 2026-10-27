CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := data-ssai
SRCS     := main.cc data.cc huffman.cc lzma.cc lzw.cc rle.cc base64.cc
OBJS     := $(SRCS:.cc=.o)
DEPS     := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

.PHONY: all clean

-include $(DEPS)
