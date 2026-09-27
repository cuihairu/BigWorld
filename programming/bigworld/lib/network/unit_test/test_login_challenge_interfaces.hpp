#ifndef TEST_LOGIN_CHALLENGE_INTERFACES_HPP
#define TEST_LOGIN_CHALLENGE_INTERFACES_HPP

#include "cstdmf/bw_map.hpp"
#include "cstdmf/watcher.hpp"

#include "connection/login_challenge_factory.hpp"


BW_BEGIN_NAMESPACE

/**
 *	A LoginChallengeConfig that hands back canned values: a child config per
 *	challenge type name, plus doubles/longs/strings by path. Anything not
 *	stored comes back as the caller's default, which is what the engine
 *	relies on for challenges that have no configuration at all.
 */
class TestChallengeConfig : public LoginChallengeConfig
{
public:
	TestChallengeConfig() :
		children_(),
		doubles_(),
		longs_(),
		strings_()
	{}

	// Takes ownership of the given child.
	void setChild( const BW::string & name, TestChallengeConfig * pChild )
	{
		children_[name] = LoginChallengeConfigPtr( pChild );
	}

	void setDouble( const BW::string & path, double value )
	{
		doubles_[path] = value;
	}

	void setLong( const BW::string & path, long value )
	{
		longs_[path] = value;
	}

	void setString( const BW::string & path, const BW::string & value )
	{
		strings_[path] = value;
	}

	virtual LoginChallengeConfigPtr getChild(
		const BW::string & childName ) const
	{
		Children::const_iterator iter = children_.find( childName );
		if (iter == children_.end())
		{
			return LoginChallengeConfigPtr();
		}

		return iter->second;
	}

	virtual const BW::string getString( const BW::string & path,
		BW::string defaultValue = BW::string() ) const
	{
		Strings::const_iterator iter = strings_.find( path );
		return iter == strings_.end() ? defaultValue : iter->second;
	}

	virtual long getLong( const BW::string & path,
		long defaultValue = 0 ) const
	{
		Longs::const_iterator iter = longs_.find( path );
		return iter == longs_.end() ? defaultValue : iter->second;
	}

	virtual double getDouble( const BW::string & path,
		double defaultValue = 0.0 ) const
	{
		Doubles::const_iterator iter = doubles_.find( path );
		return iter == doubles_.end() ? defaultValue : iter->second;
	}

private:
	typedef BW::map< BW::string, LoginChallengeConfigPtr > Children;
	typedef BW::map< BW::string, double > Doubles;
	typedef BW::map< BW::string, long > Longs;
	typedef BW::map< BW::string, BW::string > Strings;

	Children children_;
	Doubles doubles_;
	Longs longs_;
	Strings strings_;
};

BW_END_NAMESPACE

#endif // TEST_LOGIN_CHALLENGE_INTERFACES_HPP
