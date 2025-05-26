// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>

#include <m/byte_streams/byte_streams.h>
#include <m/utility/pointers.h>

namespace m
{
    namespace filesystem
    {
        ///
        /// The contract for the m::filesystem monitoring is perhaps not what you wish it to be but
        /// it is what it has to be. These paragraphs hopefully will clear this philosophical point
        /// up.
        ///
        /// The ideal goal for filesystem file change monitoring that a client might have is to have
        /// a path to a file, whether the file exists or not, and request change notifications for
        /// that path object. The change notifications would be of the sort, "The file came into
        /// existence", "the file was deleted", "the file's contents changed", "the file's metadata
        /// changed," and the like - this is not an exhaustive list.
        ///
        /// Part of the problem is that sometimes it is impossible for the user's security context
        /// to even answer whether the file exists or not. Perhaps the user has access to the
        /// container (directory) in which the file resides, but not the file itself. Perhaps that
        /// is evidence that the file exists but may or may not allow for change monitoring. But
        /// perhaps the user's security context does not even grant permission to probe the
        /// container.
        ///
        /// On normal file operations, the semantics are trivial and immediate. The operation on the
        /// file fail and the failure is reported in the form of some hopefully relevant error code
        /// or exception.
        ///
        /// But in the case of a background file monitor, what is to be done?
        ///
        /// We will come back to this issue in a moment.
        ///
        /// Ignoring the security points, let us consider just the cases of where parts of the
        /// filesystem namespace come and go. If the entire container is missing when the watch is
        /// registered, presumably the desire of the client is to be notified if/when the container
        /// is created and then the file within comes into existence.
        ///
        /// In practicality these two paths can be unified - the container has to be accessed in
        /// order to observe the file within, and if the container cannot be accessed, the client is
        /// notified with the error information about the failure and the client can then make a
        /// decision about how to proceed. It can decide to cancel the watch, or it can request that
        /// the path be probed again after some amount of time.
        ///
        /// [[design decision: is it smart to put this into the filesystem monitor? Maybe the watch
        /// always just goes cancelled and the client is responsible for setting the watch up again
        /// after some time period? on one hand, it would make the monitor easier, on the other
        /// hand, it makes clients harder. I had assumed that the monitor would do it only because
        /// it was already interacting with the win32 threadpool apis but really the win32
        /// threadpool apis for dealing with timed events are disjoint from the ones that deal with
        /// async i/o so while the documentation pages are nearby each other, the code is really not
        /// overlapping... Kind of leaning towards having it be a one and one thing and clients have
        /// to worry about this? But now this would mean that even on first go, if the file isn't
        /// there in the first place, all clients are stuck really writing a loop in a timer to try
        /// to set the watch, so the whole filesystem monitor api is only a building block for the
        /// *real* monitor API which is the one that does the polling / repeating anyhow. OK, it has
        /// to be done with the polling / repeating otherwise it's not useful.]]
        ///

        /// <summary>
        ///
        /// </summary>
        struct change_notification
        {
            /// <summary>
            /// The `on_begin` virtual function is called by the filesystem
            /// monitor prior to any other notifications.
            /// </summary>
            virtual void
            on_begin() = 0;

            /// <summary>
            /// The `requeue_directory_access_attempt` struct encapsulates the data to
            /// govern the re-queuing of a failed attempt to access a directory.
            /// </summary>
            struct requeue_directory_access_attempt
            {
                std::chrono::milliseconds m_milliseconds;
            };

            /// <summary>
            /// The `on_directory_access_failure` virtual function is called by the filesystem
            /// monitor when the directory for a monitor cannot be accessed.
            ///
            /// If the called function wishes to retry the access to the directory (a common pattern
            /// when the error is because the directory was not found), returning a
            /// `requeue_directory_access_attempt` with some reasonable number of milliseconds
            /// encoded within it.
            ///
            /// The filesystem monitor has an overall policy minimum for the requeue time, typically
            /// 1000ms, not less than 500ms.
            ///
            /// To report access errors, copy the data out of the `error` parameter and enqueue a
            /// message to some thread that the client owns. Do not do arbitrary processing on the
            /// filesystem monitor's callback threads.
            ///
            /// If the caller returns std::nullopt, the registered watch becomes cancelled. The
            /// `on_cancelled()` callback will be issued and then no further callbacks will be made.
            /// </summary>
            /// <param name="directory"></param>
            /// <param name="error"></param>
            /// <returns></returns>
            virtual std::optional<requeue_directory_access_attempt>
            on_directory_access_failure(std::filesystem::path const&             directory,
                                        std::filesystem::filesystem_error const& error) = 0;

            struct requeue_file_access_attempt
            {
                std::chrono::milliseconds m_milliseconds;
            };

            /// <summary>
            /// The `on_file_access_failure` virtual function is called by the filesystem
            /// monitor when the file for a monitor cannot be accessed.
            ///
            /// Depending on the level of support provided by the operating system platform, the
            /// filesystem monitor may be able to notify the client about changes to files without
            /// ever accessing the files. However, it may also have to access the files in order to
            /// generate hashes to observe whether they have changed. If the monitor cannot generate
            /// a hash, it may not be able to determine whether the file has changed.
            ///
            /// The hash calculation is used in two scenarios.
            ///
            /// Scenario 1 is for "primary detection". That is, when the operating system cannot
            /// give change notifications or if the operating system's notifications overrun the
            /// buffer supplied. In this case, the file is read, hashed (using a hash algorithm
            /// specified by the monitor) and then the hash is kept with the record of the watch.
            ///
            /// At some point of time in the future, when either a timer expires or when the
            /// operating system notifies the monitor that the buffer has been overrun and
            /// reprocessing is required, the files are hashed. (Note that in both of these cases,
            /// the `on_file_recheck_required()` notification is first fired before performing the
            /// recheck in case the client wishes to perform the recheck themselves.)
            ///
            /// Scenario 2 is for "hash debounce". The operating system's filesystems can generate
            /// change notifications for a file when nothing about it has changed. It's possible
            /// that these are bugs, or perhaps they are changes for internal metadata that is not
            /// observable from normal user data streams, but nonetheless you get change
            /// notifications regarding them. The question is: what are you going to do about it?
            ///
            /// During the development of this feature, on Windows 11 with NTFS, this problem was
            /// observed in spades, forcing configuration files to be repeatedly reloaded and
            /// reprocessed even when no changes had been made to them. It's unclear if some filter
            /// driver (antivirus?) or other was causing this phenomenon but it was making debugging
            /// so difficult that I added the debounce feature to quiet the noise. When a file
            /// change notification became available, the file was hashed, a timer was set for some
            /// small period of time (50ms), and after that time, it was hashed again. If the hashes
            /// were equal, the debounce was considered done.
            ///
            /// Also note that change is not necessarily atomic to files. Writers can be writing,
            /// slowly.
            ///
            /// If the called function wishes to retry the access to the directory (a common pattern
            /// when the error is because the directory was not found), returning a
            /// `requeue_directory_access_attempt` with some reasonable number of milliseconds
            /// encoded within it.
            ///
            /// The filesystem monitor has an overall policy minimum for the requeue time, typically
            /// 1000ms, not less than 500ms.
            ///
            /// To report access errors, copy the data out of the `error` parameter and enqueue a
            /// message to some thread that the client owns. Do not do arbitrary processing on the
            /// filesystem monitor's callback threads.
            ///
            /// If the caller returns std::nullopt, the registered watch becomes cancelled. The
            /// `on_cancelled()` callback will be issued and then no further callbacks will be made.
            /// </summary>
            /// <param name="directory"></param>
            /// <param name="error"></param>
            /// <returns></returns>
            virtual std::optional<requeue_file_access_attempt>
            on_file_access_failure(std::filesystem::path const&             directory,
                                   std::filesystem::path const&             filename,
                                   std::filesystem::filesystem_error const& error) = 0;

            /// <summary>
            /// The `on_file_changed` virtual function is called by the
            /// filesystem monitor after a file has been found to have been
            /// changed.
            ///
            /// Regarding the parameters, if `file` were a
            /// `std::filesystem::path`
            /// instance that referred to the file that changed, the
            /// `parent_path` parameter value would be the value returned
            /// by calling `file.parent_path()` and the `file_name` parameter
            /// value would be the value returned by calling
            /// `file.filename()`.
            ///
            /// </summary>
            /// <param name="parent_path">The parent path of the
            /// file which changed.</param>
            /// <param name="filename">The filename of the file
            /// which changed.</param>
            virtual void
            on_file_changed(std::filesystem::path const& directory,
                            std::filesystem::path const& filename) = 0;

            /// <summary>
            /// The `on_file_deleted` virtual function is called by the
            /// filesystem monitor after a file has been found to have been
            /// deleted.
            ///
            /// Regarding the parameters, if `file` were a
            /// `std::filesystem::path`
            /// instance that referred to the file was deleted, the
            /// `parent_path` parameter value would be the value returned
            /// by calling `file.parent_path()` and the `file_name` parameter
            /// value would be the value returned by calling
            /// `file.filename()`.
            ///
            /// </summary>
            /// <param name="parent_path">The parent path of the
            /// file which was deleted.</param>
            /// <param name="filename">The filename of the file
            /// which was deleted.</param>
            virtual void
            on_file_deleted(std::filesystem::path const& directory,
                            std::filesystem::path const& filename) = 0;

            /// <summary>
            /// The `on_file_recheck_required` virtual function is called by
            /// the filesystem monitor when it is unsure of the status of
            /// a file and the client must itself recheck the status of the
            /// file.
            ///
            /// Regarding the parameters, if `file` were a
            /// `std::filesystem::path`
            /// instance that referred to the file, the
            /// `parent_path` parameter value would be the value returned
            /// by calling `file.parent_path()` and the `file_name` parameter
            /// value would be the value returned by calling
            /// `file.filename()`.
            ///
            /// </summary>
            /// <param name="parent_path">The parent path of the
            /// file.</param>
            /// <param name="filename">The filename of the file.</param>
            virtual void
            on_file_recheck_required(std::filesystem::path const& directory,
                                     std::filesystem::path const& filename) = 0;

            /// <summary>
            /// The `on_cancelled` virtual function is called by the
            /// filesystem monitor when the change notification is by
            /// destroying the change notification registration token.
            ///
            /// No further notifications will be issued once
            /// on_cancelled() has been called.
            /// </summary>
            virtual void
            on_cancelled() = 0;

            /// <summary>
            /// ???
            /// </summary>
            virtual void
            on_invalid() = 0;

            //
            // Un-block-comment these after copy/paste for your derived
            // type to implement the pure virtual member functions:
            //
            // void
            // on_begin() override;
            //
            // void
            // on_file_changed(std::filesystem::path const& directory,
            //              std::filesystem::path const& filename) override;
            //
            // void
            // on_file_deleted(std::filesystem::path const& directory,
            //              std::filesystem::path const& filename) override;
            //
            // void
            // on_file_recheck_required(std::filesystem::path const& directory,
            //                         std::filesystem::path const& filename) override;
            //
            // void
            // on_cancelled() override;
            //
            // void
            // on_invalid() override;
            //
        };

        struct change_notification_registration_token
        {
            virtual ~change_notification_registration_token() = default;
        };

        class monitor
        {
        public:
            struct watch_policy_minimum_requeue_duration
            {
                std::chrono::milliseconds m_milliseconds;
            };

            using watch_policy = std::variant<watch_policy_minimum_requeue_duration>;

            /// <summary>
            /// Registers a watch on a filesystem path to receive
            /// notifications about filesystem change events about
            /// the specific file named in the path.
            ///
            /// Only individual files may be monitored.
            /// </summary>
            /// <param name="path">
            /// The `path` is a filesystem path for which `path.is_absolute()`
            /// must return true.
            /// </param>
            /// <param name="change_notification_ptr">
            /// The `change_notification_ptr` parameter is a pointer to an
            /// object which will receive notifications about changes to the
            /// file at the path until either:
            ///
            /// - The `monitor` instance is destroyed
            /// - The `change_notification_registration_token` returned by
            /// this function is destroyed.
            ///
            /// Must not be nullptr.
            /// </param>
            /// <param name="policies">
            /// </param>
            /// <returns>
            /// Returns a non-nullptr
            /// std::unique_ptr<change_notification_registration_token>
            /// that the caller uses to manage the lifetime of the change
            /// notification registration. When you no longer want to receive
            /// change notifications, delete the change notification
            /// registration token object by resetting the unique_ptr or
            /// allowing it to be otherwise destroyed.
            /// </returns>
            std::unique_ptr<change_notification_registration_token>
            register_watch(std::filesystem::path const&      path,
                           m::not_null<change_notification*> change_notification_ptr,
                           std::initializer_list<watch_policy>&& policies =
                               std::initializer_list<watch_policy>{})
            {
                return do_register_watch(path, change_notification_ptr, std::forward<std::initializer_list<watch_policy>>(policies));
            }

        protected:
            virtual ~monitor() {}

            virtual std::unique_ptr<change_notification_registration_token>
            do_register_watch(std::filesystem::path const&      path,
                              m::not_null<change_notification*> change_notification_ptr,
                              std::initializer_list<watch_policy>&& policies) = 0;
        };

        std::shared_ptr<monitor>
        get_monitor();

    } // namespace filesystem
} // namespace m