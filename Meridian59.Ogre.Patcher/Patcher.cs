﻿using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Reflection;
using System.Runtime.Serialization.Json;
using System.Threading;
using System.Windows.Forms;

namespace Meridian59.Ogre.Patcher
{
    public static class Patcher
    {
        private const string BASEURL       = "http://ww1.meridiannext.com/netclient/clientpatch/";
        private const string JSONURL       = "http://ww1.meridiannext.com/netclient/patchinfo.txt";
        private const int NUMWORKERS       = 8;
        private const int MAXRETRIES       = 3;
        private const string JSONPATCHFILE = "patchinfo.txt";
        private const string CLIENTEXE     = "Meridian59.Ogre.Client.exe";
        private const string PATCHEREXE    = "Meridian59.Ogre.Patcher.exe";
        private const string FOLDERX64     = "x64";
        private const string FOLDERX86     = "x86";
        private const string CLIENTX64     = FOLDERX64 + "/" + CLIENTEXE;
        private const string CLIENTX86     = FOLDERX86 + "/" + CLIENTEXE;

        private static readonly string[] EXCLUSIONS     = new string[] { "club.exe", PATCHEREXE };
        private static readonly ConcurrentQueue<PatchFile> queue = new ConcurrentQueue<PatchFile>();
        private static readonly List<PatchFile> files   = new List<PatchFile>();
        private static readonly Worker[] workers        = new Worker[NUMWORKERS];       
        private static readonly Stopwatch watch         = new Stopwatch();
        private static readonly WebClient webClient     = new WebClient();
        private static readonly double MSTICKDIVISOR    = (double)Stopwatch.Frequency / 1000.0;      
        private static int filesDone                    = 0;
        private static bool abort                       = false;
        private static bool isRunning                   = true;

        /// <summary>
        /// Main entry point
        /// </summary>
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            // start ticker
            watch.Start();

            // create ui
            DownloadForm progressForm = new DownloadForm(files);
            progressForm.FormClosed += OnFormClosed;
            progressForm.Show();

            // start download of patchinfo.txt
            webClient.DownloadFileCompleted += OnWebClientDownloadFileCompleted;
            webClient.DownloadFileAsync(new Uri(JSONURL), JSONPATCHFILE);
           
            ///////////////////////////////////////////////////////////////////////

            // mainthread loop
            while(isRunning)
            {
                long   tick   = watch.ElapsedTicks;
                double mstick = (double)tick / MSTICKDIVISOR;
                
                // tick ui
                progressForm.Tick(mstick);

                // process messages
                Application.DoEvents();
                
                // sleep a bit (-> ~60 FPS)
                Thread.Sleep(16);
            }

            ///////////////////////////////////////////////////////////////////////

            // also stop worker-instances
            for (int i = 0; i < workers.Length; i++)
                workers[i].Stop();
     
            // start client in case patching went well
            if (!abort)
            {
                string clientExec = Assembly.GetEntryAssembly().Location;
                string clientPath = Path.GetDirectoryName(clientExec);
                string fileName   = Environment.Is64BitOperatingSystem ? CLIENTX64 : CLIENTX86;
                string workDir    = Environment.Is64BitOperatingSystem ? FOLDERX64 : FOLDERX86;

                ProcessStartInfo pi = new ProcessStartInfo();
                pi.FileName         = Path.Combine(clientPath, fileName);
                pi.Arguments        = "";
                pi.UseShellExecute  = true;
                pi.WorkingDirectory = Path.Combine(clientPath, workDir);

                Process process = new Process();
                process.StartInfo = pi;
                process.Start();
            }
        }

        /// <summary>
        /// Parses the entries from patchinfo.txt
        /// </summary>
        /// <returns></returns>
        private static void ReadPatchInfoTxt()
        {
            // clear current instances if any
            files.Clear();

            // filestream on file
            FileStream fs = new FileStream(JSONPATCHFILE, FileMode.Open, FileAccess.Read);

            // json reader
            DataContractJsonSerializer reader = 
                new DataContractJsonSerializer(typeof(List<PatchFile>));
            
            // deserialize list of PatchFile
            List<PatchFile> list = (List<PatchFile>)reader.ReadObject(fs);

            // cleanup filestream
            fs.Close();
            fs.Dispose();

            // remove unwanted entries
            for (int i = list.Count - 1; i >= 0; i--)
            {
                PatchFile f = list[i];

                // remove files marked to be not downloaded
                if (!f.Download)
                    list.RemoveAt(i);

                // look for hardcoded exclusions
                else 
                {
                    foreach (string s in EXCLUSIONS)
                    {
                        if (f.Filename.Equals(s))
                        {
                            list.RemoveAt(i);
                            break;
                        }
                    }
                }
            }

            // add them to the real list instance
            files.AddRange(list);
        }

        private static void OnFormClosed(object sender, FormClosedEventArgs e)
        {
            // make sure to quit loop
            abort = true;
            isRunning = false;
        }

        private static void OnWebClientDownloadFileCompleted(object sender, AsyncCompletedEventArgs e)
        {
            // error downloading patchinfo.txt
            if (e.Error != null)
            {
                MessageBox.Show("Download of patchinfo.txt failed.",
                    "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);

                abort = true;
                isRunning = false;
            }
            else
            {
                // parse patchinfo.txt
                ReadPatchInfoTxt();

                // enqueue entries
                foreach (PatchFile entry in files)
                    queue.Enqueue(entry);

                // create worker-instances
                for (int i = 0; i < workers.Length; i++)
                {
                    string assemblyLocation = Assembly.GetExecutingAssembly().Location;

                    workers[i] = new Worker(
                        Path.GetDirectoryName(assemblyLocation),
                        BASEURL,
                        queue,
                        SynchronizationContext.Current);

                    workers[i].FileFinishedOK += OnWorkerFileFinishedOK;
                    workers[i].FileFinishedError += OnWorkerFileFinishedError;
                    workers[i].Start();
                }
            }
        }

        private static void OnWorkerFileFinishedOK(object sender, PatchFile.EventArgs e)
        {
            // raise counter for finished files
            filesDone++;

            // check for finish
            if (filesDone >= files.Count)
                isRunning = false;
        }

        private static void OnWorkerFileFinishedError(object sender, PatchFile.EventArgs e)
        {
            // raise errorcounter for this file
            e.File.ErrorCount++;

            // try again by enqueueing again
            if (e.File.ErrorCount < MAXRETRIES)
            {
                e.File.LengthDone = 0;
                queue.Enqueue(e.File);
            }
            else
            {
                MessageBox.Show("Download of file " + e.File.Filename + " failed.", 
                    "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            
                // make sure to quit loop
                abort = true;
                isRunning = false;
            }
        }
    }
}