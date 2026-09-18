#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

// A local note's source remains authoritative. Every displayed character maps
// back to UTF-16 source offsets so clicking a preview never rewrites Markdown.
namespace Markdown {
enum Style : unsigned { Plain=0, Bold=1, Italic=2, Code=4, Link=8, Strike=16 };
struct Span { size_t start, length; unsigned style; };
struct Block {
    std::wstring text, marker;
    std::vector<size_t> source;
    std::vector<Span> spans;
    size_t start=0, end=0;
    int heading=0, indent=0, quote=0;
    bool code=false, rule=false;
};

inline void Append(Block& block,const std::wstring& input,size_t begin,size_t end,unsigned style) {
    const size_t start=block.text.size();
    for(size_t i=begin;i<end;++i) {
        block.text+=input[i];
        block.source.push_back(i);
    }
    if(end>begin && style) {
        if(!block.spans.empty() && block.spans.back().style==style &&
           block.spans.back().start+block.spans.back().length==start)block.spans.back().length+=end-begin;
        else block.spans.push_back({start,end-begin,style});
    }
}
inline size_t Find(const std::wstring& input,const std::wstring& token,size_t from,size_t end) {
    while(from<end) {
        const size_t p=input.find(token,from);
        if(p==std::wstring::npos || p+token.size()>end)return end;
        size_t slashes=0;
        for(size_t i=p;i>0 && input[i-1]==L'\\';--i)++slashes;
        if(slashes%2==0)return p;
        from=p+token.size();
    }
    return end;
}
inline void Inline(Block& block,const std::wstring& input,size_t begin,size_t end,unsigned style=Plain,int depth=0) {
    if(depth>=16) {Append(block,input,begin,end,style);return;}
    bool noBracket=false;
    for(size_t i=begin;i<end;) {
        if(input[i]==L'\\' && i+1<end && iswpunct(input[i+1])) {
            Append(block,input,i+1,i+2,style);i+=2;continue;
        }
        if(input[i]==L'\x60') {
            size_t count=1;
            while(i+count<end && input[i+count]==L'\x60')++count;
            const size_t close=Find(input,std::wstring(count,L'\x60'),i+count,end);
            if(close<end) {
                Append(block,input,i+count,close,style|Code);i=close+count;continue;
            }
            Append(block,input,i,i+count,style);i+=count;continue;
        }
        if(input[i]==L'[' && !noBracket) {
            const size_t labelEnd=Find(input,L"]",i+1,end);
            if(labelEnd==end)noBracket=true;
            if(labelEnd+1<end && input[labelEnd+1]==L'(') {
                size_t close=labelEnd+2;int nesting=1;
                for(;close<end;++close) {
                    if(input[close]==L'\\' && close+1<end){++close;continue;}
                    if(input[close]==L'(')++nesting;
                    if(input[close]==L')' && --nesting==0)break;
                }
                if(close<end) {
                    if(i>begin && input[i-1]==L'!')Append(block,input,i,close+1,style);
                    else Inline(block,input,i+1,labelEnd,style|Link,depth+1);
                    i=close+1;continue;
                }
            }
        }
        const wchar_t c=input[i];
        if(c==L'*' || c==L'_' || c==L'~') {
            size_t count=1;
            while(i+count<end && input[i+count]==c && count<3)++count;
            if(c==L'~')count=std::min(count,size_t(2));
            const bool intraword=c==L'_' && i>begin && iswalnum(input[i-1]);
            if(!intraword && (c!=L'~' || count==2) && i+count<end && !iswspace(input[i+count])) {
                const std::wstring token(count,c);
                size_t close=Find(input,token,i+count,end);
                while(close<end) {
                    size_t run=1;
                    while(close+run<end && input[close+run]==c)++run;
                    if(!iswspace(input[close-1]) && !(count==1 && run%2==0) &&
                       !(c==L'_' && close+run<end && iswalnum(input[close+run]))) {
                        // Leave the inner emphasis's closing delimiters inside
                        // the recursive range, e.g. **bold *italic***.
                        close+=run-count;break;
                    }
                    close=Find(input,token,close+run,end);
                }
                if(close<end && close>i+count) {
                    const unsigned added=c==L'~'?Strike:count==3?(Bold|Italic):count==2?Bold:Italic;
                    Inline(block,input,i+count,close,style|added,depth+1);i=close+count;continue;
                }
            }
            Append(block,input,i,i+count,style);i+=count;continue;
        }
        Append(block,input,i,i+1,style);++i;
    }
}

inline std::vector<Block> Parse(const std::wstring& input) {
    std::vector<Block> blocks;
    wchar_t fence=0;size_t fenceLength=0;bool hasCodeLine=false;
    for(size_t start=0;start<input.size();) {
        size_t end=input.find_first_of(L"\r\n",start);
        if(end==std::wstring::npos)end=input.size();
        size_t next=end;
        if(next<input.size() && input[next]==L'\r')++next;
        if(next<input.size() && input[next]==L'\n')++next;
        size_t p=start;int spaces=0;
        while(p<end && (input[p]==L' ' || input[p]==L'\t'))spaces+=input[p++]==L'\t'?4:1;
        size_t fenceCount=0;
        if(p<end && (input[p]==L'\x60' || input[p]==L'~'))
            while(p+fenceCount<end && input[p+fenceCount]==input[p])++fenceCount;
        size_t fenceTail=p+fenceCount;
        while(fenceTail<end && iswspace(input[fenceTail]))++fenceTail;
        if(fence) {
            Block& block=blocks.back();block.end=next;
            if(spaces<=3 && p<end && input[p]==fence && fenceCount>=fenceLength && fenceTail==end)fence=0;
            else {
                if(hasCodeLine) {
                    block.text+=L'\n';block.source.push_back(start>0?start-1:start);
                }
                Append(block,input,start,end,Code);
                hasCodeLine=true;
            }
            start=next;continue;
        }
        Block block;block.start=start;block.end=next;
        if(spaces<=3 && fenceCount>=3) {
            fence=input[p];fenceLength=fenceCount;hasCodeLine=false;block.code=true;
            blocks.push_back(std::move(block));start=next;continue;
        }
        while(p<end && input[p]==L'>') {
            ++block.quote;++p;
            if(p<end && input[p]==L' ')++p;
        }
        size_t content=p;
        while(content<end && input[content]==L'#' && content-p<6)++content;
        if(content>p && (content==end || input[content]==L' ')) {
            block.heading=static_cast<int>(content-p);p=content;
            while(p<end && input[p]==L' ')++p;
            size_t tail=end;
            while(tail>p && input[tail-1]==L' ')--tail;
            size_t hashes=tail;
            while(hashes>p && input[hashes-1]==L'#')--hashes;
            if(hashes<tail && hashes>p && input[hashes-1]==L' ')end=hashes-1;
        } else {
            size_t ruleCount=0;wchar_t ruleChar=p<end?input[p]:0;bool rule=true;
            for(size_t j=p;j<end;++j) {
                if(input[j]==ruleChar)++ruleCount;
                else if(!iswspace(input[j])){rule=false;break;}
            }
            block.rule=rule && ruleCount>=3 && (ruleChar==L'-' || ruleChar==L'*' || ruleChar==L'_');
            if(!block.rule && p+1<end && (input[p]==L'-' || input[p]==L'*' || input[p]==L'+') && input[p+1]==L' ') {
                block.marker=L"•";p+=2;
            } else if(!block.rule) {
                size_t number=p;
                while(number<end && iswdigit(input[number]))++number;
                if(number>p && number+1<end && (input[number]==L'.' || input[number]==L')') && input[number+1]==L' ') {
                    block.marker=input.substr(p,number-p)+L".";p=number+2;
                }
            }
            if(!block.marker.empty()) {
                block.indent=spaces/2;
                if(p+2<end && input[p]==L'[' && input[p+2]==L']' &&
                   (input[p+1]==L' ' || input[p+1]==L'x' || input[p+1]==L'X') &&
                   (p+3==end || input[p+3]==L' ')) {
                    block.marker=input[p+1]==L' '?L"☐":L"☑";p+=3;
                }
                while(p<end && input[p]==L' ')++p;
            } else if(block.quote==0 && block.heading==0)p=start;
        }
        if(!block.rule)Inline(block,input,p,end);
        blocks.push_back(std::move(block));start=next;
    }
    return blocks;
}
} // namespace Markdown
