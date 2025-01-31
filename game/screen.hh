#pragma once

#include "controllers.hh"
#include <SDL_events.h>
#include <string>
#include <memory>

class Audio;

/// Abstract Class for screens
class Screen {
  public:
	/// counstructor
	Screen(std::string const& name): m_name(name) {}
	virtual ~Screen() {}
	/// Event handler for navigation events
	virtual void manageEvent(input::NavEvent const& event) = 0;
	/// Event handler for SDL events
	virtual void manageEvent(SDL_Event) {}
	/// prepare screen for drawing
	virtual void prepare() {}
	/// draws screen
	virtual void draw() = 0;
	/// enters screen
	virtual void enter() = 0;
	/// exits screen
	virtual void exit() = 0;
	/// reloads OpenGL textures but avoids restarting music etc.
	virtual void reloadGL() { exit(); enter(); }
	/// returns screen name
	std::string getName() const { return m_name; }

  private:
	std::string m_name;
};

