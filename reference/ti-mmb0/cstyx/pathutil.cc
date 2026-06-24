
#include "pathutil.hh"

using namespace std;

void pathutil::normalise(vector<string>& path)
{
	vector<string> npath;
	vector<string>::iterator i;

	for (i=path.begin();i!=path.end();++i) {
		if (*i=="..") {
			if (npath.size()) npath.pop_back();
		} else if (*i!="."&&i->size()) {
			npath.push_back(*i);
		}
	}
	path=npath;
}

void pathutil::normalise(string &path)
{
	vector<string> v;

	if (!path.size()) return;
	pathutil::split(path,v);
	pathutil::normalise(v);
	if (path[0]=='/')
		path="/";
	else
		path.erase();
	pathutil::join(v,path);
}

void pathutil::join(const vector<string>& path, string& s)
{
	vector<string>::const_iterator i=path.begin();

	if (!path.size()) return;
	s+=*i++;
	for (;i!=path.end();++i) {
		s+="/";
		s+=*i;
	}
}

/* found at:
http://oopweb.com/CPP/Documents/CPPHOWTO/Volume/C++Programming-HOWTO-7.html
*/
void pathutil::split(const string& str, vector<string>& tokens)
{
	// Skip delimiters at beginning.
	string::size_type lastPos = str.find_first_not_of("/", 0);
	// Find first "non-delimiter".
	string::size_type pos     = str.find_first_of("/", lastPos);

	while (string::npos != pos || string::npos != lastPos) {
		// Found a token, add it to the vector.
		tokens.push_back(str.substr(lastPos, pos - lastPos));
		// Skip delimiters.  Note the "not_of"
		lastPos = str.find_first_not_of("/", pos);
		// Find next "non-delimiter"
		pos = str.find_first_of("/", lastPos);
	}
}

string pathutil::name(const string &p)
{
	vector<string> pl;

	pathutil::split(p,pl);
	if (!pl.size()) return "";
	return pl.back();
}

#ifdef PATHUTIL_TEST

#include <iostream>

void printvec(const vector<string>& v)
{
	vector<string>::const_iterator i=v.begin();
	int index;

	cout<<"Length "<<v.size()<<'\n';
	for (index=0;i!=v.end();++i)
		cout<<index++<<" ["<<*i<<"]\n";
}

void testsplit(const string& s)
{
	vector<string> v;
	pathutil::split(s,v);
	cout<<"Split of "<<s<<":\n\n";
	printvec(v);
	cout<<'\n';
}

void testnorm(const string& s)
{
	vector<string> v;
	string s2;

	cout<<"Normalise of "<<s<<":\n\n";
	pathutil::split(s,v);
	pathutil::normalise(v);
	printvec(v);
	pathutil::join(v,s2);
	cout<<"\nJoins as ["<<s2<<"]\n\n";
}

void testnormstr(const string& s)
{
	string s2=s;

	pathutil::normalise(s2);
	cout<<"String normalise of ["<<s<<"]: ["<<s2<<"]\n\n";
}

void testname(const string& s)
{
	cout<<"Name of ["<<s<<"]: ["<<pathutil::name(s)<<"]\n\n";
}

int main()
{
	string path;
	vector<string> v;

	// split
	testsplit("foo/bar/baz");
	testsplit("/");
	testsplit("//foo//bar/");
	testsplit("/foo/bar/");
	testsplit("bar/bar");
	testsplit("foo");
	testsplit("foo bar / baz");

	// normalise
	testnorm("foo/bar/baz");
	testnorm("../../..");
	testnorm("/./foo/bar/../baz/");
	testnorm("/./.././.././../..");
	testnorm("........");
	testnorm("/baz/../");
	testnorm("");
	
	// string normalise
	testnormstr("foo/bar/baz");
	testnormstr("../../..");
	testnormstr("/./foo/bar/../baz/");
	testnormstr("/./.././.././../..");
	testnormstr("........");
	testnormstr("/baz/../");
	testnormstr("");

	// name
	testname("foo");
	testname("foo/bar");
	testname("");
	testname("////");
	testname("bar/../baz");
	testname("/bar/bar/baz/");

	return 0;
}

#endif
