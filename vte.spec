Name: vte
Version: 0.3.17
Release: 1
Summary: An experimental terminal emulator.
License: LGPL
Group: User Interface/X
BuildRoot: %{_tmppath}/%{name}-root
Source: %{name}-%{version}.tar.gz

%description
VTE is an experimental terminal emulator widget.  This package contains
a sample application which places the widget in a terminal window.

%package devel
Summary: Files needed for developing applications which use vte.
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}

%description devel
VTE is an experimental terminal emulator widget.  This package contains
the files needed for building applications using the widget.

%prep
%setup -q

%build
%configure --enable-shared --enable-static
make

%clean
rm -fr $RPM_BUILD_ROOT

%install
rm -fr $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root)
%doc ChangeLog COPYING HACKING NEWS README
%{_bindir}/*
%{_libdir}/*.so.*.*
%{_datadir}/*

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/*.a
%{_libdir}/*.so
%{_libdir}/pkgconfig/*

%changelog
* Thu May 16 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.17-1
- tweak finding of selected text

* Wed May 15 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.16-1
- hook up Insert->kI
- convert scroll events to button 4/5 if an app wants mouse events
- don't send drag events when apps only want click events
- fix selection crashbug

* Tue May 14 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.15-1
- fix ce, implement save/restore mode

* Tue May 14 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.14-1
- don't draw nul chars

* Mon May 13 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.13-1
- fix insert mode, implement visual bells

* Thu May  9 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.12-1
- iconv and remapping from otaylor
- implement custom tabstopping

* Wed May  8 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.11-1
- add mouse drag event handling

* Mon May  6 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.10-1
- do mouse autohide and cursor switching

* Mon May  6 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.9-1
- start handling mouse events

* Mon May  6 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.8-1
- handle window manipulation sequences

* Fri May  3 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.7-1
- discard invalid control sequences
- recognize designate-??-character-set correctly

* Thu May  2 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.6-1
- add a couple of sequence handlers, fix a couple of accessibility crashbugs

* Thu May  2 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.5-1
- fix cap parsing error and "handle" long invalid multibyte sequences better

* Thu May  2 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.4-1
- try to speed up sequence recognition a bit
- disable some window_scroll speedups that appear to cause flickering

* Wed May  1 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.2-1
- include a small default termcap for systems without termcap files

* Tue Apr 30 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3.1-1
- disconnect from the configure_toplevel signal at finalize-time

* Tue Apr 30 2002 Nalin Dahyabhai <nalin@redhat.com> 0.3-1
- add an accessiblity object

* Mon Apr 29 2002 Nalin Dahyabhai <nalin@redhat.com> 0.2.3-1
- fix color resetting
- fix idle handlers not being disconnected

* Mon Apr 29 2002 Nalin Dahyabhai <nalin@redhat.com> 0.2.2-1
- bug fixes

* Thu Apr 25 2002 Nalin Dahyabhai <nalin@redhat.com> 0.1-1
- finish initial packaging, start the changelog
